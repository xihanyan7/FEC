#include "internal/fec_decoder.h"

#include "internal/fec_constants.h"
#include "internal/fec_frame.h"
#include "internal/fec_profile.h"
#include "internal/fec_symbol.h"
#include "internal/reed_solomon_codec.h"
#include "internal/xor_codec.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace fec {
namespace internal {
namespace {

/**
 * @brief 生成低 bit_count 位为 1 的 32 位掩码。
 * @param bit_count [in] 有效位数，取值范围 1..32。
 * @return 对应的低位掩码。
 */
uint32_t full_mask(uint8_t bit_count) {
    return bit_count == 32u ?
        0xFFFFFFFFu : ((static_cast<uint32_t>(1u) << bit_count) - 1u);
}

/**
 * @brief 统计 32 位整数中置位位元的数量。
 * @param value [in] 待统计的位图。
 * @return 值为 1 的位元数量。
 */
uint32_t popcount32(uint32_t value) {
    uint32_t count = 0u;
    while (value != 0u) {
        value &= value - 1u;
        ++count;
    }
    return count;
}

} // namespace

struct Decoder::Slot {
    bool used;
    fec_profile_t profile;
    ProfileParams params;
    uint32_t block_id;
    uint32_t base_seq;
    uint32_t first_seen_ms;
    uint32_t source_mask;
    uint32_t repair_mask;
    uint32_t emitted_mask;
    ReedSolomonCodec rs_codec;

    /** @brief 构造未使用的活动块槽位并清零块参数。 */
    Slot()
        : used(false), profile(FEC_PROFILE_NONE), params(), block_id(0u),
          base_seq(0u), first_seen_ms(0u), source_mask(0u), repair_mask(0u),
          emitted_mask(0u), rs_codec() {
        std::memset(&params, 0, sizeof(params));
    }

    /**
     * @brief 用首帧信息初始化槽位并配置块级 RS 编解码器。
     * @param next_profile [in] 新块的 profile。
     * @param next_params [in] 新块的规范化 profile 参数。
     * @param next_block_id [in] 新块编号。
     * @param next_base_seq [in] 新块首源序号。
     * @param now_ms [in] 新块首帧到达时间。
     * @return 初始化和算法配置均成功时返回 true。
     */
    bool initialize(fec_profile_t next_profile,
                    const ProfileParams &next_params,
                    uint32_t next_block_id,
                    uint32_t next_base_seq,
                    uint32_t now_ms) {
        used = true;
        profile = next_profile;
        params = next_params;
        block_id = next_block_id;
        base_seq = next_base_seq;
        first_seen_ms = now_ms;
        source_mask = 0u;
        repair_mask = 0u;
        emitted_mask = 0u;
        if (!rs_codec.configure(params)) {
            used = false;
            return false;
        }
        return true;
    }

    /** @brief 将槽位置为未使用并清空接收、修复和交付位图。 */
    void clear() {
        used = false;
        source_mask = 0u;
        repair_mask = 0u;
        emitted_mask = 0u;
    }
};

struct Decoder::RetiredKey {
    bool used;
    fec_profile_t profile;
    uint32_t block_id;
    uint32_t base_seq;
    uint8_t xor_group_size;

    /** @brief 构造未使用的退休块键。 */
    RetiredKey()
        : used(false), profile(FEC_PROFILE_NONE), block_id(0u), base_seq(0u),
          xor_group_size(0u) {}
};

Decoder::Decoder()
    : config_(), packet_callback_(nullptr), event_callback_(nullptr), user_(nullptr),
      max_active_blocks_(0u), symbol_size_(0u), slot_stride_(0u), slots_(),
      storage_(), retired_(), retired_count_(0u), retired_cursor_(0u), stats_() {
    std::memset(&config_, 0, sizeof(config_));
    std::memset(&stats_, 0, sizeof(stats_));
}

Decoder::~Decoder() {}

int Decoder::initialize(const fec_config_t &config,
                        fec_packet_callback packet_callback,
                        fec_event_callback event_callback,
                        void *user) {
    config_ = config;
    packet_callback_ = packet_callback;
    event_callback_ = event_callback;
    user_ = user;
    max_active_blocks_ = config.max_active_blocks == 0u ?
        4u : config.max_active_blocks;
    symbol_size_ = config.symbol_size;
    slot_stride_ = kDecoderBufferCount * symbol_size_;
    retired_count_ = std::max<std::size_t>(
        16u, static_cast<std::size_t>(max_active_blocks_) * 4u);

    slots_.reset(new (std::nothrow) Slot[max_active_blocks_]);
    storage_.reset(new (std::nothrow) uint8_t[
        static_cast<std::size_t>(max_active_blocks_) * slot_stride_]);
    retired_.reset(new (std::nothrow) RetiredKey[retired_count_]);
    if (!slots_ || !storage_ || !retired_) {
        return FEC_ERR_NOMEM;
    }
    reset();
    std::memset(&stats_, 0, sizeof(stats_));
    return FEC_OK;
}

int Decoder::ingest(const uint8_t *frame,
                    std::size_t frame_size,
                    uint32_t now_ms) {
    ParsedFrame parsed;
    const int parse_status = FrameCodec::parse(frame, frame_size, parsed);
    if (parse_status != FEC_OK) {
        return record_error(parse_status);
    }

    if (parsed.profile != FEC_PROFILE_XOR_DX && parsed.xor_group_size != 0u) {
        return record_error(FEC_ERR_FORMAT);
    }
    ProfileParams params;
    if (!get_profile_params(parsed.profile, parsed.xor_group_size, params) ||
        (config_.profile != FEC_PROFILE_AUTO && config_.profile != parsed.profile)) {
        return record_error(FEC_ERR_PROFILE);
    }
    if (config_.profile == FEC_PROFILE_XOR_DX &&
        config_.xor_group_size != parsed.xor_group_size) {
        return record_error(FEC_ERR_PROFILE);
    }
    if (parsed.payload_size > symbol_size_ ||
        parsed.stream_id != config_.stream_id ||
        parsed.session_epoch != config_.session_epoch ||
        parsed.symbol_size != symbol_size_ ||
        parsed.type > FEC_FRAME_REPAIR || parsed.index >= params.total_count) {
        return record_error(FEC_ERR_FORMAT);
    }
    if (parsed.type == FEC_FRAME_SOURCE) {
        if (parsed.index >= params.source_count ||
            parsed.payload_size > config_.max_packet_size) {
            return record_error(FEC_ERR_FORMAT);
        }
    } else if (parsed.profile == FEC_PROFILE_NONE ||
               parsed.index < params.source_count ||
               parsed.payload_size != symbol_size_) {
        return record_error(FEC_ERR_FORMAT);
    }

    ++stats_.valid_frames;
    const int retired = retired_state(parsed.block_id, parsed.base_seq,
                                      parsed.profile, parsed.xor_group_size);
    if (retired > 0) {
        ++stats_.duplicate_or_late_frames;
        return FEC_OK;
    }
    if (retired < 0) {
        return record_error(FEC_ERR_FORMAT);
    }

    Slot *slot = find_slot(parsed.block_id);
    if (slot != nullptr &&
        (slot->profile != parsed.profile || slot->base_seq != parsed.base_seq ||
         slot->params.source_count != params.source_count)) {
        return record_error(FEC_ERR_FORMAT);
    }
    if (slot == nullptr) {
        slot = create_slot(parsed.block_id, parsed.base_seq, parsed.profile,
                           params, now_ms);
        if (slot == nullptr) {
            return FEC_ERR_BUSY;
        }
    }

    const std::size_t slot_index = static_cast<std::size_t>(slot - slots_.get());
    uint8_t *memory = slot_memory(slot_index);
    if (parsed.type == FEC_FRAME_SOURCE) {
        if (!receive_source(*slot, memory, parsed.index, parsed.payload,
                            parsed.payload_size, parsed.payload_crc)) {
            return FEC_OK;
        }
    } else if (!receive_repair(*slot, memory, parsed.index, parsed.payload)) {
        return FEC_OK;
    }

    if (is_xor_algorithm(params.algorithm)) {
        try_xor_decode(*slot);
    } else if (params.algorithm == FEC_ALGORITHM_REED_SOLOMON) {
        try_rs_decode(*slot);
    }
    if ((slot->source_mask & full_mask(params.source_count)) ==
        full_mask(params.source_count)) {
        retire_slot(slot_index, RetireReason::Completed);
    }
    return FEC_OK;
}

int Decoder::expire(uint32_t now_ms) {
    if (config_.block_timeout_ms == 0u) {
        return FEC_OK;
    }
    for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
        if (slots_[i].used &&
            now_ms - slots_[i].first_seen_ms >= config_.block_timeout_ms) {
            retire_slot(i, RetireReason::Expired);
        }
    }
    return FEC_OK;
}

int Decoder::flush() {
    for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
        if (slots_[i].used) {
            retire_slot(i, RetireReason::Expired);
        }
    }
    return FEC_OK;
}

int Decoder::reset() {
    for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
        slots_[i].clear();
    }
    std::memset(storage_.get(), 0,
                static_cast<std::size_t>(max_active_blocks_) * slot_stride_);
    for (std::size_t i = 0u; i < retired_count_; ++i) {
        retired_[i] = RetiredKey();
    }
    retired_cursor_ = 0u;
    return FEC_OK;
}

int Decoder::get_stats(fec_decoder_stats_t &out_stats, bool reset_after_read) {
    out_stats = stats_;
    if (reset_after_read) {
        std::memset(&stats_, 0, sizeof(stats_));
    }
    return FEC_OK;
}

uint8_t *Decoder::slot_memory(std::size_t slot_index) {
    return storage_.get() + slot_index * slot_stride_;
}

void Decoder::remember_retired(const Slot &slot) {
    RetiredKey &key = retired_[retired_cursor_];
    key.used = true;
    key.profile = slot.profile;
    key.block_id = slot.block_id;
    key.base_seq = slot.base_seq;
    key.xor_group_size = slot.profile == FEC_PROFILE_XOR_DX ?
        slot.params.source_count : 0u;
    retired_cursor_ = (retired_cursor_ + 1u) % retired_count_;
}

int Decoder::retired_state(uint32_t block_id,
                           uint32_t base_seq,
                           fec_profile_t profile,
                           uint8_t xor_group_size) const {
    for (std::size_t i = 0u; i < retired_count_; ++i) {
        const RetiredKey &key = retired_[i];
        if (!key.used || key.block_id != block_id) {
            continue;
        }
        return key.profile == profile && key.base_seq == base_seq &&
               key.xor_group_size == xor_group_size ? 1 : -1;
    }
    return 0;
}

int Decoder::record_error(int status) {
    if (status == FEC_ERR_CRC) {
        ++stats_.crc_errors;
    } else if (status == FEC_ERR_PROFILE) {
        ++stats_.profile_errors;
    } else if (status == FEC_ERR_FORMAT || status == FEC_ERR_TOO_LARGE) {
        ++stats_.format_errors;
    }
    return status;
}

Decoder::Slot *Decoder::find_slot(uint32_t block_id) {
    for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
        if (slots_[i].used && slots_[i].block_id == block_id) {
            return &slots_[i];
        }
    }
    return nullptr;
}

Decoder::Slot *Decoder::create_slot(uint32_t block_id,
                                    uint32_t base_seq,
                                    fec_profile_t profile,
                                    const ProfileParams &params,
                                    uint32_t now_ms) {
    std::size_t slot_index = max_active_blocks_;
    for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
        if (!slots_[i].used) {
            slot_index = i;
            break;
        }
    }
    if (slot_index == max_active_blocks_) {
        uint32_t largest_age = 0u;
        slot_index = 0u;
        for (std::size_t i = 0u; i < max_active_blocks_; ++i) {
            const uint32_t age = now_ms - slots_[i].first_seen_ms;
            if (i == 0u || age > largest_age) {
                largest_age = age;
                slot_index = i;
            }
        }
        retire_slot(slot_index, RetireReason::Evicted);
    }

    Slot &slot = slots_[slot_index];
    if (!slot.initialize(profile, params, block_id, base_seq, now_ms)) {
        return nullptr;
    }
    std::memset(slot_memory(slot_index), 0, slot_stride_);
    return &slot;
}

bool Decoder::receive_source(Slot &slot,
                             uint8_t *memory,
                             uint16_t index,
                             const uint8_t *payload,
                             uint16_t payload_size,
                             uint32_t payload_crc) {
    const uint32_t bit = static_cast<uint32_t>(1u) << index;
    if ((slot.source_mask & bit) != 0u) {
        ++stats_.duplicate_or_late_frames;
        return false;
    }
    slot.source_mask |= bit;
    ++stats_.source_frames;

    if (is_xor_algorithm(slot.params.algorithm)) {
        const uint8_t column = static_cast<uint8_t>(
            index % slot.params.interleave_columns);
        uint8_t *temporary = memory +
            static_cast<std::size_t>(kMaxInterleaveColumns) * 2u * symbol_size_;
        SymbolCodec::make(temporary, symbol_size_, payload, payload_size, payload_crc);
        XorCodec::accumulate(
            memory + static_cast<std::size_t>(column) * symbol_size_,
            temporary,
            symbol_size_);
    } else if (slot.params.algorithm == FEC_ALGORITHM_REED_SOLOMON) {
        SymbolCodec::make(memory + static_cast<std::size_t>(index) * symbol_size_,
                          symbol_size_, payload, payload_size, payload_crc);
    }
    slot.emitted_mask |= bit;
    packet_callback_(user_, slot.base_seq + index, payload, payload_size, 0);
    return true;
}

bool Decoder::receive_repair(Slot &slot,
                             uint8_t *memory,
                             uint16_t index,
                             const uint8_t *payload) {
    const uint8_t repair_index = static_cast<uint8_t>(
        index - slot.params.source_count);
    const uint32_t bit = static_cast<uint32_t>(1u) << repair_index;
    if ((slot.repair_mask & bit) != 0u) {
        ++stats_.duplicate_or_late_frames;
        return false;
    }
    slot.repair_mask |= bit;
    ++stats_.repair_frames;
    if (is_xor_algorithm(slot.params.algorithm)) {
        std::memcpy(memory +
                        (static_cast<std::size_t>(kMaxInterleaveColumns) +
                         repair_index) * symbol_size_,
                    payload,
                    symbol_size_);
    } else {
        std::memcpy(memory + static_cast<std::size_t>(index) * symbol_size_,
                    payload,
                    symbol_size_);
    }
    return true;
}

int Decoder::emit_recovered(Slot &slot,
                            uint8_t source_index,
                            const uint8_t *symbol) {
    const uint32_t bit = static_cast<uint32_t>(1u) << source_index;
    if ((slot.emitted_mask & bit) != 0u) {
        return FEC_OK;
    }
    std::size_t packet_size = 0u;
    if (!SymbolCodec::validate(symbol, symbol_size_, config_.max_packet_size,
                               packet_size)) {
        ++stats_.recovery_crc_errors;
        return FEC_ERR_CRC;
    }
    slot.emitted_mask |= bit;
    ++stats_.recovered_packets;
    packet_callback_(user_, slot.base_seq + source_index,
                     symbol + 6u, packet_size, 1);
    return FEC_OK;
}

void Decoder::try_xor_decode(Slot &slot) {
    const std::size_t slot_index = static_cast<std::size_t>(&slot - slots_.get());
    uint8_t *memory = slot_memory(slot_index);
    uint8_t *repairs = memory +
        static_cast<std::size_t>(kMaxInterleaveColumns) * symbol_size_;
    uint8_t *recovered = memory +
        static_cast<std::size_t>(kMaxInterleaveColumns) * 2u * symbol_size_;

    for (uint8_t column = 0u;
         column < slot.params.interleave_columns;
         ++column) {
        uint8_t received_count = 0u;
        uint8_t missing_row = 0u;
        for (uint8_t row = 0u; row < slot.params.interleave_rows; ++row) {
            const uint8_t index = static_cast<uint8_t>(
                row * slot.params.interleave_columns + column);
            if ((slot.source_mask &
                 (static_cast<uint32_t>(1u) << index)) != 0u) {
                ++received_count;
            } else {
                missing_row = row;
            }
        }
        if (received_count + 1u == slot.params.interleave_rows &&
            (slot.repair_mask &
             (static_cast<uint32_t>(1u) << column)) != 0u) {
            XorCodec::recover(
                repairs + static_cast<std::size_t>(column) * symbol_size_,
                memory + static_cast<std::size_t>(column) * symbol_size_,
                recovered,
                symbol_size_);
            const uint8_t missing_index = static_cast<uint8_t>(
                missing_row * slot.params.interleave_columns + column);
            (void)emit_recovered(slot, missing_index, recovered);
        }
    }
}

void Decoder::try_rs_decode(Slot &slot) {
    const uint32_t available = slot.source_mask |
        (slot.repair_mask << slot.params.source_count);
    if (popcount32(available) < slot.params.source_count) {
        return;
    }
    uint8_t selected[kMaxEncodedSymbols];
    uint8_t inverse[kMaxEncodedSymbols][kMaxEncodedSymbols];
    if (!slot.rs_codec.prepare_recovery(available, selected, inverse)) {
        return;
    }
    const std::size_t slot_index = static_cast<std::size_t>(&slot - slots_.get());
    uint8_t *symbols = slot_memory(slot_index);
    uint8_t *recovered = symbols +
        static_cast<std::size_t>(kMaxEncodedSymbols) * symbol_size_;
    for (uint8_t missing = 0u; missing < slot.params.source_count; ++missing) {
        const uint32_t bit = static_cast<uint32_t>(1u) << missing;
        if ((slot.emitted_mask & bit) != 0u) {
            continue;
        }
        slot.rs_codec.recover_source(missing, selected, inverse, symbols,
                                     symbol_size_, recovered);
        (void)emit_recovered(slot, missing, recovered);
    }
}

void Decoder::update_loss_shape_stats(const Slot &slot) {
    uint32_t current_burst = 0u;
    for (uint8_t index = 0u; index < slot.params.source_count; ++index) {
        const uint32_t bit = static_cast<uint32_t>(1u) << index;
        if ((slot.source_mask & bit) == 0u) {
            ++current_burst;
            stats_.max_missing_burst =
                std::max(stats_.max_missing_burst, current_burst);
        } else {
            current_burst = 0u;
        }
    }
    if (slot.params.algorithm == FEC_ALGORITHM_XOR_INTERLEAVED) {
        for (uint8_t column = 0u;
             column < slot.params.interleave_columns;
             ++column) {
            uint8_t missing = 0u;
            for (uint8_t row = 0u; row < slot.params.interleave_rows; ++row) {
                const uint8_t index = static_cast<uint8_t>(
                    row * slot.params.interleave_columns + column);
                if ((slot.source_mask &
                     (static_cast<uint32_t>(1u) << index)) == 0u) {
                    ++missing;
                }
            }
            if (missing >= 2u) {
                ++stats_.same_column_loss_groups;
            }
        }
    }
}

void Decoder::retire_slot(std::size_t slot_index, RetireReason reason) {
    Slot &slot = slots_[slot_index];
    if (!slot.used) {
        return;
    }
    const uint32_t expected_mask = full_mask(slot.params.source_count);
    const uint32_t raw_missing = slot.params.source_count -
        popcount32(slot.source_mask & expected_mask);
    const uint32_t unrecoverable = slot.params.source_count -
        popcount32(slot.emitted_mask & expected_mask);
    stats_.source_packets_expected += slot.params.source_count;
    stats_.raw_missing_packets += raw_missing;
    stats_.unrecoverable_packets += unrecoverable;
    if (unrecoverable == 0u) {
        ++stats_.completed_blocks;
    }
    if (reason == RetireReason::Expired) {
        ++stats_.expired_blocks;
    } else if (reason == RetireReason::Evicted) {
        ++stats_.evicted_blocks;
    }
    update_loss_shape_stats(slot);

    if (event_callback_ != nullptr) {
        for (uint8_t index = 0u; index < slot.params.source_count; ++index) {
            const uint32_t bit = static_cast<uint32_t>(1u) << index;
            if ((slot.emitted_mask & bit) == 0u) {
                event_callback_(user_, slot.base_seq + index,
                                FEC_ERR_UNRECOVERABLE);
            }
        }
    }
    remember_retired(slot);
    slot.clear();
}

} // namespace internal
} // namespace fec

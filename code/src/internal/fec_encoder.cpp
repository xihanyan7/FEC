#include "internal/fec_encoder.h"

#include "internal/fec_frame.h"
#include "internal/fec_symbol.h"
#include "internal/xor_codec.h"

#include <cstring>
#include <new>

namespace fec {
namespace internal {

Encoder::Encoder()
    : config_(), params_(), active_profile_(FEC_PROFILE_NONE),
      pending_profile_(FEC_PROFILE_NONE), callback_(nullptr), user_(nullptr),
      block_id_(0u), base_seq_(0u), source_count_(0u), frame_capacity_(0u),
      frame_(), xor_accumulators_(), rs_symbols_(), rs_codec_() {
    std::memset(&config_, 0, sizeof(config_));
    std::memset(&params_, 0, sizeof(params_));
}

int Encoder::initialize(const fec_config_t &config,
                        fec_frame_callback callback,
                        void *user) {
    ProfileParams initial;
    if (!get_profile_params(config.profile, initial)) {
        return FEC_ERR_PROFILE;
    }
    config_ = config;
    callback_ = callback;
    user_ = user;
    active_profile_ = config.profile;
    pending_profile_ = config.profile;
    frame_capacity_ = kHeaderSize + config_.symbol_size + 4u;

    frame_.reset(new (std::nothrow) uint8_t[frame_capacity_]);
    xor_accumulators_.reset(new (std::nothrow) uint8_t[
        static_cast<std::size_t>(kMaxInterleaveColumns) * config_.symbol_size]);
    rs_symbols_.reset(new (std::nothrow) uint8_t[
        static_cast<std::size_t>(kMaxEncodedSymbols) * config_.symbol_size]);
    if (!frame_ || !xor_accumulators_ || !rs_symbols_) {
        return FEC_ERR_NOMEM;
    }
    return activate_profile(config.profile) ? FEC_OK : FEC_ERR_PROFILE;
}

int Encoder::push(const uint8_t *data, std::size_t size) {
    if (data == nullptr && size != 0u) {
        return FEC_ERR_ARGUMENT;
    }
    if (size > config_.max_packet_size) {
        return FEC_ERR_TOO_LARGE;
    }
    /* profile 只在块边界切换，避免一个块内使用两套编码参数。 */
    if (source_count_ == 0u && pending_profile_ != active_profile_ &&
        !activate_profile(pending_profile_)) {
        return FEC_ERR_PROFILE;
    }

    const uint16_t source_index = source_count_;
    int status = emit_frame(FEC_FRAME_SOURCE, source_index, data, size);
    if (status != FEC_OK) {
        return status;
    }
    const uint32_t packet_crc = FrameCodec::crc32(data, size);

    if (active_profile_ == FEC_PROFILE_NONE) {
        ++block_id_;
        ++base_seq_;
        return FEC_OK;
    }

    if (params_.algorithm == FEC_ALGORITHM_XOR_INTERLEAVED) {
        const uint8_t column = static_cast<uint8_t>(
            source_index % params_.interleave_columns);
        uint8_t *temporary_symbol = frame_.get() + kHeaderSize;
        SymbolCodec::make(temporary_symbol, config_.symbol_size,
                          data, size, packet_crc);
        XorCodec::accumulate(
            xor_accumulators_.get() +
                static_cast<std::size_t>(column) * config_.symbol_size,
            temporary_symbol,
            config_.symbol_size);
    } else {
        SymbolCodec::make(
            rs_symbols_.get() +
                static_cast<std::size_t>(source_index) * config_.symbol_size,
            config_.symbol_size, data, size, packet_crc);
    }

    ++source_count_;
    if (source_count_ != params_.source_count) {
        return FEC_OK;
    }
    status = emit_repairs();
    if (status != FEC_OK) {
        return status;
    }
    finish_block(params_.source_count);
    if (pending_profile_ != active_profile_ &&
        !activate_profile(pending_profile_)) {
        return FEC_ERR_PROFILE;
    }
    return FEC_OK;
}

int Encoder::flush() {
    if (source_count_ != 0u) {
        /* 不完整块不生成 repair，防止残留符号跨块参与运算。 */
        finish_block(source_count_);
    }
    return FEC_OK;
}

int Encoder::request_profile(fec_profile_t profile) {
    ProfileParams ignored;
    if (!get_profile_params(profile, ignored)) {
        return FEC_ERR_PROFILE;
    }
    pending_profile_ = profile;
    return FEC_OK;
}

fec_profile_t Encoder::profile() const {
    return active_profile_;
}

bool Encoder::activate_profile(fec_profile_t profile) {
    ProfileParams next;
    if (!get_profile_params(profile, next) || !rs_codec_.configure(next)) {
        return false;
    }
    active_profile_ = profile;
    params_ = next;
    clear_workspaces();
    return true;
}

void Encoder::clear_workspaces() {
    std::memset(xor_accumulators_.get(), 0,
                static_cast<std::size_t>(kMaxInterleaveColumns) *
                    config_.symbol_size);
    std::memset(rs_symbols_.get(), 0,
                static_cast<std::size_t>(kMaxEncodedSymbols) *
                    config_.symbol_size);
}

void Encoder::finish_block(uint16_t consumed_sources) {
    base_seq_ += consumed_sources;
    source_count_ = 0u;
    ++block_id_;
    clear_workspaces();
}

int Encoder::emit_repairs() {
    if (params_.algorithm == FEC_ALGORITHM_XOR_INTERLEAVED) {
        for (uint8_t column = 0u;
             column < params_.interleave_columns;
             ++column) {
            const int status = emit_frame(
                FEC_FRAME_REPAIR,
                static_cast<uint16_t>(params_.source_count + column),
                xor_accumulators_.get() +
                    static_cast<std::size_t>(column) * config_.symbol_size,
                config_.symbol_size);
            if (status != FEC_OK) {
                return status;
            }
        }
        return FEC_OK;
    }

    for (uint8_t encoded = params_.source_count;
         encoded < params_.total_count;
         ++encoded) {
        uint8_t *repair = xor_accumulators_.get();
        rs_codec_.encode_repair(encoded, rs_symbols_.get(),
                                config_.symbol_size, repair);
        const int status = emit_frame(FEC_FRAME_REPAIR, encoded, repair,
                                      config_.symbol_size);
        if (status != FEC_OK) {
            return status;
        }
    }
    return FEC_OK;
}

int Encoder::emit_frame(fec_frame_type_t type,
                        uint16_t index,
                        const uint8_t *payload,
                        std::size_t payload_size) {
    FrameFields fields;
    fields.profile = active_profile_;
    fields.type = type;
    fields.stream_id = config_.stream_id;
    fields.session_epoch = config_.session_epoch;
    fields.block_id = block_id_;
    fields.base_seq = base_seq_;
    fields.index = index;
    fields.symbol_size = config_.symbol_size;

    std::size_t frame_size = 0u;
    const int status = FrameCodec::write(frame_.get(), frame_capacity_, fields,
                                         payload, payload_size, frame_size);
    if (status == FEC_OK) {
        callback_(user_, frame_.get(), frame_size);
    }
    return status;
}

} // namespace internal
} // namespace fec

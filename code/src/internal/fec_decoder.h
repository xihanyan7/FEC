#ifndef FEC_INTERNAL_DECODER_H
#define FEC_INTERNAL_DECODER_H

#include "fec/fec.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fec {
namespace internal {

class Decoder {
public:
    Decoder();
    ~Decoder();

    int initialize(const fec_config_t &config,
                   fec_packet_callback packet_callback,
                   fec_event_callback event_callback,
                   void *user);
    int ingest(const uint8_t *frame, std::size_t frame_size, uint32_t now_ms);
    int expire(uint32_t now_ms);
    int flush();
    int reset();
    int get_stats(fec_decoder_stats_t &out_stats, bool reset_after_read);

private:
    struct Slot;
    struct RetiredKey;
    enum class RetireReason { Completed, Expired, Evicted };

    uint8_t *slot_memory(std::size_t slot_index);
    void remember_retired(const Slot &slot);
    int retired_state(uint32_t block_id,
                      uint32_t base_seq,
                      fec_profile_t profile) const;
    int record_error(int status);
    Slot *find_slot(uint32_t block_id);
    Slot *create_slot(uint32_t block_id,
                      uint32_t base_seq,
                      fec_profile_t profile,
                      uint32_t now_ms);
    bool receive_source(Slot &slot,
                        uint8_t *memory,
                        uint16_t index,
                        const uint8_t *payload,
                        uint16_t payload_size,
                        uint32_t payload_crc);
    bool receive_repair(Slot &slot,
                        uint8_t *memory,
                        uint16_t index,
                        const uint8_t *payload);
    int emit_recovered(Slot &slot,
                       uint8_t source_index,
                       const uint8_t *symbol);
    void try_xor_decode(Slot &slot);
    void try_rs_decode(Slot &slot);
    void update_loss_shape_stats(const Slot &slot);
    void retire_slot(std::size_t slot_index, RetireReason reason);

    fec_config_t config_;
    fec_packet_callback packet_callback_;
    fec_event_callback event_callback_;
    void *user_;
    uint8_t max_active_blocks_;
    uint16_t symbol_size_;
    std::size_t slot_stride_;
    std::unique_ptr<Slot[]> slots_;
    std::unique_ptr<uint8_t[]> storage_;
    std::unique_ptr<RetiredKey[]> retired_;
    std::size_t retired_count_;
    std::size_t retired_cursor_;
    fec_decoder_stats_t stats_;
};

} // namespace internal
} // namespace fec

#endif

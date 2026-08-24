#ifndef FEC_INTERNAL_ENCODER_H
#define FEC_INTERNAL_ENCODER_H

#include "fec/fec.h"
#include "internal/fec_constants.h"
#include "internal/fec_profile.h"
#include "internal/reed_solomon_codec.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fec {
namespace internal {

class Encoder {
public:
    Encoder();

    int initialize(const fec_config_t &config,
                   fec_frame_callback callback,
                   void *user);
    int push(const uint8_t *data, std::size_t size);
    int flush();
    int request_profile(fec_profile_t profile);
    fec_profile_t profile() const;

private:
    bool activate_profile(fec_profile_t profile);
    void clear_workspaces();
    void finish_block(uint16_t consumed_sources);
    int emit_repairs();
    int emit_frame(fec_frame_type_t type,
                   uint16_t index,
                   const uint8_t *payload,
                   std::size_t payload_size);

    fec_config_t config_;
    ProfileParams params_;
    fec_profile_t active_profile_;
    fec_profile_t pending_profile_;
    fec_frame_callback callback_;
    void *user_;
    uint32_t block_id_;
    uint32_t base_seq_;
    uint16_t source_count_;
    std::size_t frame_capacity_;
    std::unique_ptr<uint8_t[]> frame_;
    std::unique_ptr<uint8_t[]> xor_accumulators_;
    std::unique_ptr<uint8_t[]> rs_symbols_;
    ReedSolomonCodec rs_codec_;
};

} // namespace internal
} // namespace fec

#endif

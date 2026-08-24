#ifndef FEC_INTERNAL_REED_SOLOMON_CODEC_H
#define FEC_INTERNAL_REED_SOLOMON_CODEC_H

#include "internal/fec_constants.h"
#include "internal/fec_profile.h"

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

class ReedSolomonCodec {
public:
    ReedSolomonCodec();

    bool configure(const ProfileParams &params);

    void encode_repair(uint8_t encoded_index,
                       const uint8_t *source_symbols,
                       std::size_t symbol_size,
                       uint8_t *repair) const;

    bool prepare_recovery(uint32_t available_mask,
                          uint8_t selected[kMaxEncodedSymbols],
                          uint8_t inverse[kMaxEncodedSymbols]
                                         [kMaxEncodedSymbols]) const;

    void recover_source(uint8_t source_index,
                        const uint8_t selected[kMaxEncodedSymbols],
                        const uint8_t inverse[kMaxEncodedSymbols]
                                             [kMaxEncodedSymbols],
                        const uint8_t *encoded_symbols,
                        std::size_t symbol_size,
                        uint8_t *output) const;

private:
    ProfileParams params_;
    uint8_t generator_[kMaxEncodedSymbols][kMaxEncodedSymbols];
};

} // namespace internal
} // namespace fec

#endif

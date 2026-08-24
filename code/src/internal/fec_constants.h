#ifndef FEC_INTERNAL_CONSTANTS_H
#define FEC_INTERNAL_CONSTANTS_H

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

static const uint16_t kMagic = 0xC1FEu;
static const uint8_t kVersion = 1u;
static const std::size_t kHeaderSize = 32u;
static const uint8_t kMaxInterleaveColumns = 8u;
static const uint8_t kMaxEncodedSymbols = 10u;
static const std::size_t kDecoderBufferCount =
    static_cast<std::size_t>(kMaxInterleaveColumns) * 2u + 1u;

} // namespace internal
} // namespace fec

#endif

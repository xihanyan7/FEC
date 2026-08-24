#include "internal/xor_codec.h"

#include <cstring>

namespace fec {
namespace internal {

void XorCodec::accumulate(uint8_t *destination,
                          const uint8_t *source,
                          std::size_t size) {
    std::size_t offset = 0u;
    for (; offset + sizeof(uint32_t) <= size; offset += sizeof(uint32_t)) {
        uint32_t left = 0u;
        uint32_t right = 0u;
        std::memcpy(&left, destination + offset, sizeof(left));
        std::memcpy(&right, source + offset, sizeof(right));
        left ^= right;
        std::memcpy(destination + offset, &left, sizeof(left));
    }
    for (; offset < size; ++offset) {
        destination[offset] ^= source[offset];
    }
}

void XorCodec::recover(const uint8_t *repair,
                       const uint8_t *received_accumulator,
                       uint8_t *output,
                       std::size_t size) {
    std::memcpy(output, repair, size);
    accumulate(output, received_accumulator, size);
}

} // namespace internal
} // namespace fec

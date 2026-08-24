#ifndef FEC_INTERNAL_SYMBOL_H
#define FEC_INTERNAL_SYMBOL_H

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

class SymbolCodec {
public:
    static void make(uint8_t *destination,
                     std::size_t symbol_size,
                     const uint8_t *packet,
                     std::size_t packet_size,
                     uint32_t packet_crc);

    static bool validate(const uint8_t *symbol,
                         std::size_t symbol_size,
                         std::size_t max_packet_size,
                         std::size_t &packet_size);
};

} // namespace internal
} // namespace fec

#endif

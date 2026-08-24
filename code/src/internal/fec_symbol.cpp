#include "internal/fec_symbol.h"

#include "internal/fec_frame.h"

#include <cstring>

namespace fec {
namespace internal {
namespace {

void put_u16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
}

void put_u32(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
    out[2] = static_cast<uint8_t>(value >> 16u);
    out[3] = static_cast<uint8_t>(value >> 24u);
}

uint16_t get_u16(const uint8_t *in) {
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8u);
}

uint32_t get_u32(const uint8_t *in) {
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8u) |
           (static_cast<uint32_t>(in[2]) << 16u) |
           (static_cast<uint32_t>(in[3]) << 24u);
}

} // namespace

void SymbolCodec::make(uint8_t *destination,
                       std::size_t symbol_size,
                       const uint8_t *packet,
                       std::size_t packet_size,
                       uint32_t packet_crc) {
    std::memset(destination, 0, symbol_size);
    put_u16(destination, static_cast<uint16_t>(packet_size));
    put_u32(destination + 2u, packet_crc);
    if (packet_size != 0u) {
        std::memcpy(destination + 6u, packet, packet_size);
    }
}

bool SymbolCodec::validate(const uint8_t *symbol,
                           std::size_t symbol_size,
                           std::size_t max_packet_size,
                           std::size_t &packet_size) {
    if (symbol_size < 6u) {
        return false;
    }
    packet_size = get_u16(symbol);
    if (packet_size > max_packet_size || packet_size > symbol_size - 6u) {
        return false;
    }
    return FrameCodec::crc32(symbol + 6u, packet_size) == get_u32(symbol + 2u);
}

} // namespace internal
} // namespace fec

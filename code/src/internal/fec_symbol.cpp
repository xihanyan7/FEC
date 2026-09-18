#include "internal/fec_symbol.h"

#include "internal/fec_frame.h"

#include <cstring>

namespace fec {
namespace internal {
namespace {

/**
 * @brief 按保护符号格式的小端序写入 16 位无符号整数。
 * @param out [out] 至少可写 2 字节的目标地址。
 * @param value [in] 待写入数值。
 */
void put_u16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
}

/**
 * @brief 按保护符号格式的小端序写入 32 位无符号整数。
 * @param out [out] 至少可写 4 字节的目标地址。
 * @param value [in] 待写入数值。
 */
void put_u32(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
    out[2] = static_cast<uint8_t>(value >> 16u);
    out[3] = static_cast<uint8_t>(value >> 24u);
}

/**
 * @brief 从保护符号的小端字节序读取 16 位无符号整数。
 * @param in [in] 至少包含 2 字节的输入地址。
 * @return 解码后的主机整数。
 */
uint16_t get_u16(const uint8_t *in) {
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8u);
}

/**
 * @brief 从保护符号的小端字节序读取 32 位无符号整数。
 * @param in [in] 至少包含 4 字节的输入地址。
 * @return 解码后的主机整数。
 */
uint32_t get_u32(const uint8_t *in) {
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8u) |
           (static_cast<uint32_t>(in[2]) << 16u) |
           (static_cast<uint32_t>(in[3]) << 24u);
}

} // namespace

/**
 * @brief 将变长原始包封装成包含长度、CRC 和零填充的定长保护符号。
 * @param destination [out] 接收保护符号的缓冲区。
 * @param symbol_size [in] 固定符号长度。
 * @param packet [in] 原始包数据。
 * @param packet_size [in] 原始包长度。
 * @param packet_crc [in] 原始包的 CRC32。
 */
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

/**
 * @brief 校验恢复符号中的包长和 CRC，并解析真实包长度。
 * @param symbol [in] 待校验的保护符号。
 * @param symbol_size [in] 保护符号长度。
 * @param max_packet_size [in] 允许的最大原始包长度。
 * @param packet_size [out] 校验成功时接收真实原始包长度。
 * @return 长度和 CRC 均有效返回 true，否则返回 false。
 */
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

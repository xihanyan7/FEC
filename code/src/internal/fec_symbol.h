#ifndef FEC_INTERNAL_SYMBOL_H
#define FEC_INTERNAL_SYMBOL_H

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

class SymbolCodec {
public:
    /**
     * @brief 将变长原始包封装成固定长度保护符号，写入长度、CRC 和零填充。
     * @param destination [out] 接收保护符号的缓冲区，容量至少为 symbol_size。
     * @param symbol_size [in] 固定保护符号长度，单位为字节。
     * @param packet [in] 原始包首地址。
     * @param packet_size [in] 原始包长度，必须能放入符号的数据区。
     * @param packet_crc [in] 原始包 CRC32，写入符号供恢复后校验。
     */
    static void make(uint8_t *destination,
                     std::size_t symbol_size,
                     const uint8_t *packet,
                     std::size_t packet_size,
                     uint32_t packet_crc);

    /**
     * @brief 校验保护符号中的长度和原始包 CRC，并解析真实包长。
     * @param symbol [in] 待校验的完整保护符号。
     * @param symbol_size [in] 符号缓冲区长度，单位为字节。
     * @param max_packet_size [in] 允许输出的最大原始包长度。
     * @param packet_size [out] 校验成功时接收符号记录的真实包长。
     * @return 校验成功返回 true，长度非法或 CRC 不匹配返回 false。
     */
    static bool validate(const uint8_t *symbol,
                         std::size_t symbol_size,
                         std::size_t max_packet_size,
                         std::size_t &packet_size);
};

} // namespace internal
} // namespace fec

#endif

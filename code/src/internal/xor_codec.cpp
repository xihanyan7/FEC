#include "internal/xor_codec.h"

#include <cstring>

namespace fec {
namespace internal {

/**
 * @brief 将 source 逐字节异或累加到 destination。
 * @param destination [in,out] 保存已有值并接收累加结果的目标缓冲区。
 * @param source [in] 待加入的等长源缓冲区。
 * @param size [in] 参与异或的字节数。
 */
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

/**
 * @brief 用 repair 异或已接收源符号的累加结果，恢复唯一缺失符号。
 * @param repair [in] XOR 修复符号。
 * @param received_accumulator [in] 已收到相关源符号的异或累加值。
 * @param output [out] 接收恢复出的源符号。
 * @param size [in] 参与恢复的字节数。
 */
void XorCodec::recover(const uint8_t *repair,
                       const uint8_t *received_accumulator,
                       uint8_t *output,
                       std::size_t size) {
    std::memcpy(output, repair, size);
    accumulate(output, received_accumulator, size);
}

} // namespace internal
} // namespace fec

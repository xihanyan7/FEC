#ifndef FEC_INTERNAL_XOR_CODEC_H
#define FEC_INTERNAL_XOR_CODEC_H

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

/* XOR 编解码不持有上下文；交织列映射和缓冲区生命周期由编解码器管理。 */
class XorCodec {
public:
    /**
     * @brief 将一个等长源符号逐字节异或累加到目标缓冲区。
     * @param destination [in,out] 累加缓冲区，调用前应按需要清零或保留已有累加值。
     * @param source [in] 待加入的源符号缓冲区。
     * @param size [in] 两个缓冲区参与运算的字节数。
     */
    static void accumulate(uint8_t *destination,
                           const uint8_t *source,
                           std::size_t size);

    /**
     * @brief 使用 repair 与已收到源符号的异或累加值恢复唯一缺失符号。
     * @param repair [in] XOR 修复符号。
     * @param received_accumulator [in] 所有已收到相关源符号的异或结果。
     * @param output [out] 接收恢复出的完整源符号。
     * @param size [in] 三个缓冲区参与运算的字节数。
     */
    static void recover(const uint8_t *repair,
                        const uint8_t *received_accumulator,
                        uint8_t *output,
                        std::size_t size);
};

} // namespace internal
} // namespace fec

#endif

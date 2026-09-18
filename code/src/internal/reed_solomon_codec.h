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
    /** @brief 构造未配置的 RS 编解码器，并清零内部生成矩阵。 */
    ReedSolomonCodec();

    /**
     * @brief 按 profile 参数构造系统型 RS 生成矩阵。
     * @param params [in] RS 源符号数、修复符号数等规范化参数。
     * @return 参数合法且生成矩阵成功时返回 true，否则返回 false。
     */
    bool configure(const ProfileParams &params);

    /**
     * @brief 计算指定编码行对应的 RS 修复符号。
     * @param encoded_index [in] 编码符号索引，应指向修复行。
     * @param source_symbols [in] 连续存放的全部源符号。
     * @param symbol_size [in] 每个源符号及输出符号的字节数。
     * @param repair [out] 接收计算得到的修复符号。
     */
    void encode_repair(uint8_t encoded_index,
                       const uint8_t *source_symbols,
                       std::size_t symbol_size,
                       uint8_t *repair) const;

    /**
     * @brief 从可用编码符号中选择方程并计算恢复矩阵。
     * @param available_mask [in] 编码符号可用位图，第 i 位对应第 i 个编码符号。
     * @param selected [out] 接收被选择的 source_count 个编码符号索引。
     * @param inverse [out] 接收所选生成矩阵子矩阵在 GF(256) 上的逆矩阵。
     * @return 可用符号足够且矩阵可逆时返回 true，否则返回 false。
     */
    bool prepare_recovery(uint32_t available_mask,
                          uint8_t selected[kMaxEncodedSymbols],
                          uint8_t inverse[kMaxEncodedSymbols]
                                         [kMaxEncodedSymbols]) const;

    /**
     * @brief 使用预先求得的逆矩阵恢复一个指定源符号。
     * @param source_index [in] 待恢复源符号在源块中的索引。
     * @param selected [in] prepare_recovery 选择的编码符号索引数组。
     * @param inverse [in] prepare_recovery 输出的 GF(256) 逆矩阵。
     * @param encoded_symbols [in] 按编码索引连续存放的符号工作区。
     * @param symbol_size [in] 单个符号长度，单位为字节。
     * @param output [out] 接收恢复出的完整源符号。
     */
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

#ifndef FEC_INTERNAL_ENCODER_H
#define FEC_INTERNAL_ENCODER_H

#include "fec/fec.h"
#include "internal/fec_constants.h"
#include "internal/fec_profile.h"
#include "internal/reed_solomon_codec.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fec {
namespace internal {

class Encoder {
public:
    /** @brief 构造空编码器；使用前必须调用 initialize。 */
    Encoder();

    /**
     * @brief 初始化配置、回调和预分配工作区。
     * @param config [in] 已由 C API 规范化的编码配置。
     * @param callback [in] FEC 帧同步输出回调。
     * @param user [in,out] 原样传给 callback 的用户上下文。
     * @return FEC_OK 表示成功，否则返回参数、内存或 profile 错误码。
     */
    int initialize(const fec_config_t &config,
                   fec_frame_callback callback,
                   void *user);
    /**
     * @brief 编码一个原始包并输出 SOURCE；块完成时继续输出 REPAIR。
     * @param data [in] 原始包首地址。
     * @param size [in] 原始包长度，单位为字节。
     * @return FEC_OK 表示成功，否则返回参数、长度或帧编码错误码。
     */
    int push(const uint8_t *data, std::size_t size);
    /**
     * @brief 丢弃未完成块状态，并在块边界应用待切换 profile。
     * @return 始终返回 FEC_OK。
     */
    int flush();
    /**
     * @brief 请求立即或在下个块边界切换编码 profile。
     * @param profile [in] 目标固定 profile；不得为 AUTO。
     * @return FEC_OK 表示请求成功，profile 无效返回 FEC_ERR_PROFILE。
     */
    int request_profile(fec_profile_t profile);
    /**
     * @brief 查询当前实际编码 profile。
     * @return 当前活动 profile。
     */
    fec_profile_t profile() const;

private:
    /**
     * @brief 解析并启用指定 profile，同时重新配置对应算法模块。
     * @param profile [in] 待启用的固定 profile。
     * @return profile 合法且算法配置成功时返回 true。
     */
    bool activate_profile(fec_profile_t profile);
    /** @brief 清零 XOR、RS 等当前块工作区，无入参和返回值。 */
    void clear_workspaces();
    /**
     * @brief 完成当前块、推进块号和源序号，并应用待切换 profile。
     * @param consumed_sources [in] 当前块实际消耗的源包数量。
     */
    void finish_block(uint16_t consumed_sources);
    /**
     * @brief 根据当前算法和块状态计算并同步输出全部修复帧。
     * @return FEC_OK 表示全部输出成功，否则返回首个帧编码错误码。
     */
    int emit_repairs();
    /**
     * @brief 组装并通过回调同步输出单个 FEC 帧。
     * @param type [in] SOURCE 或 REPAIR 帧类型。
     * @param index [in] 该帧在块内的源/编码索引。
     * @param payload [in] 待封装 payload 首地址。
     * @param payload_size [in] payload 长度，单位为字节。
     * @return FEC_OK 表示输出成功，否则返回帧序列化错误码。
     */
    int emit_frame(fec_frame_type_t type,
                   uint16_t index,
                   const uint8_t *payload,
                   std::size_t payload_size);

    fec_config_t config_;
    ProfileParams params_;
    fec_profile_t active_profile_;
    fec_profile_t pending_profile_;
    fec_frame_callback callback_;
    void *user_;
    uint32_t block_id_;
    uint32_t base_seq_;
    uint16_t source_count_;
    std::size_t frame_capacity_;
    std::unique_ptr<uint8_t[]> frame_;
    std::unique_ptr<uint8_t[]> xor_accumulators_;
    std::unique_ptr<uint8_t[]> rs_symbols_;
    ReedSolomonCodec rs_codec_;
};

} // namespace internal
} // namespace fec

#endif

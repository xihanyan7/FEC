#ifndef FEC_INTERNAL_DECODER_H
#define FEC_INTERNAL_DECODER_H

#include "fec/fec.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fec {
namespace internal {

struct ProfileParams;

class Decoder {
public:
    /** @brief 构造空解码器并清零所有句柄与统计状态。 */
    Decoder();
    /** @brief 析构解码器并释放预分配的槽位和符号工作区。 */
    ~Decoder();

    /**
     * @brief 初始化解码配置、回调及固定大小工作区。
     * @param config [in] 已由 C API 规范化的解码配置。
     * @param packet_callback [in] 原始包同步输出回调。
     * @param event_callback [in] 不可恢复事件回调，可为空。
     * @param user [in,out] 原样传给两个回调的用户上下文。
     * @return FEC_OK 表示成功，否则返回参数、容量或内存错误码。
     */
    int initialize(const fec_config_t &config,
                   fec_packet_callback packet_callback,
                   fec_event_callback event_callback,
                   void *user);
    /**
     * @brief 解析并接收一个 FEC 帧，必要时创建块、输出源包或尝试恢复。
     * @param frame [in] 完整 FEC 帧首地址。
     * @param frame_size [in] 输入帧长度，单位为字节。
     * @param now_ms [in] 调用方单调时钟当前毫秒值。
     * @return FEC_OK 表示已处理，否则返回格式、CRC、profile 等错误码。
     */
    int ingest(const uint8_t *frame, std::size_t frame_size, uint32_t now_ms);
    /**
     * @brief 淘汰超过配置超时时间的活动块。
     * @param now_ms [in] 与 ingest 相同时间基准的当前毫秒值。
     * @return FEC_OK。
     */
    int expire(uint32_t now_ms);
    /** @brief 以流结束原因关闭全部活动块；返回 FEC_OK。 */
    int flush();
    /** @brief 清空块、迟到包记录和工作区，但保留累计统计；返回 FEC_OK。 */
    int reset();
    /**
     * @brief 复制当前统计快照并可在读取后清零统计。
     * @param out_stats [out] 接收当前统计数据。
     * @param reset_after_read [in] 为 true 时在复制后清零内部统计。
     * @return FEC_OK。
     */
    int get_stats(fec_decoder_stats_t &out_stats, bool reset_after_read);

private:
    struct Slot;
    struct RetiredKey;
    enum class RetireReason { Completed, Expired, Evicted };

    /**
     * @brief 获取指定活动槽位对应的预分配符号工作区。
     * @param slot_index [in] 槽位数组索引。
     * @return 槽位工作区首地址。
     */
    uint8_t *slot_memory(std::size_t slot_index);
    /**
     * @brief 将已退休块的键写入环形迟到包记录。
     * @param slot [in] 即将退休的活动块槽位。
     */
    void remember_retired(const Slot &slot);
    /**
     * @brief 查询帧是否属于已退休块。
     * @param block_id [in] 帧块编号。
     * @param base_seq [in] 块首源序号。
     * @param profile [in] 帧 profile。
     * @param xor_group_size [in] XOR_DX 分组大小。
     * @return 迟到匹配状态；零表示未找到。
     */
    int retired_state(uint32_t block_id,
                      uint32_t base_seq,
                      fec_profile_t profile,
                      uint8_t xor_group_size) const;
    /**
     * @brief 将指定输入错误计入对应统计项并原样返回状态码。
     * @param status [in] 待记录的 fec_status_t 错误码。
     * @return 与 status 相同的错误码。
     */
    int record_error(int status);
    /**
     * @brief 按块编号查找活动槽位。
     * @param block_id [in] 目标块编号。
     * @return 找到时返回槽位指针，否则返回空指针。
     */
    Slot *find_slot(uint32_t block_id);
    /**
     * @brief 创建活动块；无空槽时先淘汰最早槽位。
     * @param block_id [in] 新块编号。
     * @param base_seq [in] 新块首源序号。
     * @param profile [in] 新块使用的 profile。
     * @param params [in] 新块的规范化 profile 参数。
     * @param now_ms [in] 首帧到达时间。
     * @return 成功时返回新槽位指针，初始化失败时返回空指针。
     */
    Slot *create_slot(uint32_t block_id,
                      uint32_t base_seq,
                      fec_profile_t profile,
                      const ProfileParams &params,
                      uint32_t now_ms);
    /**
     * @brief 接收、校验并保存 SOURCE，同时立即向上层交付原始包。
     * @param slot [in,out] SOURCE 所属活动块。
     * @param memory [in,out] 该槽位的符号工作区。
     * @param index [in] 源符号块内索引。
     * @param payload [in] SOURCE 中的原始包数据。
     * @param payload_size [in] 原始包长度。
     * @param payload_crc [in] 帧头携带的原始包 CRC32。
     * @return 首次成功接收返回 true，重复或非法输入返回 false。
     */
    bool receive_source(Slot &slot,
                        uint8_t *memory,
                        uint16_t index,
                        const uint8_t *payload,
                        uint16_t payload_size,
                        uint32_t payload_crc);
    /**
     * @brief 接收并保存一个 REPAIR 符号。
     * @param slot [in,out] REPAIR 所属活动块。
     * @param memory [in,out] 该槽位的符号工作区。
     * @param index [in] 修复符号块内索引。
     * @param payload [in] 长度等于 symbol_size 的修复符号。
     * @return 首次成功接收返回 true，重复输入返回 false。
     */
    bool receive_repair(Slot &slot,
                        uint8_t *memory,
                        uint16_t index,
                        const uint8_t *payload);
    /**
     * @brief 校验恢复符号并通过回调交付其中的原始包。
     * @param slot [in,out] 恢复包所属活动块。
     * @param source_index [in] 被恢复源符号的块内索引。
     * @param symbol [in] 已恢复的完整保护符号。
     * @return FEC_OK 表示交付成功，校验失败返回 FEC_ERR_CRC。
     */
    int emit_recovered(Slot &slot,
                       uint8_t source_index,
                       const uint8_t *symbol);
    /**
     * @brief 扫描 XOR 列并恢复其中唯一缺失的源符号。
     * @param slot [in,out] 待尝试恢复的 XOR 活动块。
     */
    void try_xor_decode(Slot &slot);
    /**
     * @brief 在可用编码符号足够时执行 RS 矩阵恢复。
     * @param slot [in,out] 待尝试恢复的 RS 活动块。
     */
    void try_rs_decode(Slot &slot);
    /**
     * @brief 在块退休前统计原始缺包数、最长突发和同列多丢包。
     * @param slot [in] 待汇总的活动块。
     */
    void update_loss_shape_stats(const Slot &slot);
    /**
     * @brief 汇总、通知并清理指定活动块。
     * @param slot_index [in] 待退休槽位索引。
     * @param reason [in] 完成、超时或容量淘汰原因。
     */
    void retire_slot(std::size_t slot_index, RetireReason reason);

    fec_config_t config_;
    fec_packet_callback packet_callback_;
    fec_event_callback event_callback_;
    void *user_;
    uint8_t max_active_blocks_;
    uint16_t symbol_size_;
    std::size_t slot_stride_;
    std::unique_ptr<Slot[]> slots_;
    std::unique_ptr<uint8_t[]> storage_;
    std::unique_ptr<RetiredKey[]> retired_;
    std::size_t retired_count_;
    std::size_t retired_cursor_;
    fec_decoder_stats_t stats_;
};

} // namespace internal
} // namespace fec

#endif

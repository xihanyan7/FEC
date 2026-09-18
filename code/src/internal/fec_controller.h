#ifndef FEC_INTERNAL_CONTROLLER_H
#define FEC_INTERNAL_CONTROLLER_H

#include "fec/fec.h"

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

/**
 * @brief 校验自适应控制器配置的 profile、阈值及窗口参数。
 * @param config [in] 待校验的控制器配置。
 * @return 所有约束满足时返回 true，否则返回 false。
 */
bool controller_config_valid(const fec_controller_config_t &config);

class AdaptiveController {
public:
    /** @brief 构造采用安全默认状态的控制器；使用前应调用 initialize。 */
    AdaptiveController();

    /**
     * @brief 保存控制参数并重置 profile、窗口和降级候选状态。
     * @param config [in] 已校验的控制器配置。
     */
    void initialize(const fec_controller_config_t &config);
    /**
     * @brief 聚合链路指标并更新建议的 FEC profile。
     * @param metrics [in] 一个或多个设备的链路指标数组。
     * @param count [in] metrics 数组元素个数。
     * @param now_ms [in] 调用方单调时钟当前毫秒值。
     * @param profile [out] 接收更新后的建议 profile。
     * @param changed [out] 接收 profile 是否在本次调用中发生变化。
     * @return FEC_OK 表示成功，否则返回参数或 profile 错误码。
     */
    int update(const fec_link_metrics_t *metrics,
               std::size_t count,
               uint32_t now_ms,
               fec_profile_t &profile,
               int &changed);
    /** @brief 返回当前建议 profile。 */
    fec_profile_t profile() const;

private:
    /**
     * @brief 根据聚合后的最差链路指标选择目标 profile。
     * @param worst_loss [in] 最差原始丢包率，单位为 ppm。
     * @param worst_residual [in] 最差 FEC 后残余丢包率，单位为 ppm。
     * @param worst_burst [in] 最长连续丢包长度。
     * @param worst_same_column [in] 交织同列多丢包事件数。
     * @return 与当前链路状态匹配的目标 profile。
     */
    fec_profile_t choose_profile(uint32_t worst_loss,
                                 uint32_t worst_residual,
                                 uint16_t worst_burst,
                                 uint16_t worst_same_column) const;
    /**
     * @brief 按保持时间和稳定窗口规则应用升级或降级决策。
     * @param desired [in] 本窗口根据指标选择出的目标 profile。
     * @param now_ms [in] 调用方单调时钟当前毫秒值。
     * @param profile [out] 接收决策后的 profile。
     * @param changed [out] 接收 profile 是否真正发生变化。
     */
    void apply_profile(fec_profile_t desired,
                       uint32_t now_ms,
                       fec_profile_t &profile,
                       int &changed);

    fec_controller_config_t config_;
    fec_profile_t current_profile_;
    fec_profile_t downgrade_candidate_;
    uint32_t last_change_ms_;
    uint32_t last_window_ms_;
    uint8_t stable_windows_;
};

} // namespace internal
} // namespace fec

#endif

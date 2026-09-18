#include "internal/fec_controller.h"

#include "internal/fec_profile.h"

#include <algorithm>
#include <cstring>

namespace fec {
namespace internal {
namespace {

/**
 * @brief 读取可选阈值；配置为零时采用默认值。
 * @param configured [in] 调用方配置值。
 * @param fallback [in] configured 为零时使用的默认值。
 * @return 实际生效的阈值。
 */
uint32_t effective_threshold(uint32_t configured, uint32_t fallback) {
    return configured == 0u ? fallback : configured;
}

/**
 * @brief 查询固定 profile 的冗余开销百万分比。
 * @param profile [in] 待查询的 profile。
 * @return profile 有效时返回冗余率，无效时返回零。
 */
uint32_t profile_overhead_ppm(fec_profile_t profile) {
    ProfileParams params;
    return get_profile_params(profile, params) ?
        profile_redundancy_ppm(params) : 0u;
}

} // namespace

/**
 * @brief 校验控制器 profile 和各级阈值之间的取值关系。
 * @param config [in] 待校验的控制器配置。
 * @return 配置有效返回 true，否则返回 false。
 */
bool controller_config_valid(const fec_controller_config_t &config) {
    ProfileParams params;
    if (!get_profile_params(config.initial_profile, params)) {
        return false;
    }
    const uint32_t high =
        effective_threshold(config.high_loss_threshold_ppm, 150000u);
    const uint32_t medium =
        effective_threshold(config.medium_loss_threshold_ppm, 50000u);
    const uint32_t low =
        effective_threshold(config.low_loss_threshold_ppm, 30000u);
    const uint32_t residual =
        effective_threshold(config.residual_loss_threshold_ppm, 20000u);
    return high <= 1000000u && medium <= high && low <= medium &&
           residual <= 1000000u;
}

/**
 * @brief 构造采用安全初始状态的自适应控制器。
 */
AdaptiveController::AdaptiveController()
    : config_(), current_profile_(FEC_PROFILE_NONE),
      downgrade_candidate_(FEC_PROFILE_NONE), last_change_ms_(0u),
      last_window_ms_(0u), stable_windows_(0u) {
    std::memset(&config_, 0, sizeof(config_));
}

/**
 * @brief 保存控制配置并将当前 profile 和降级候选重置为初始值。
 * @param config [in] 已校验的控制器配置。
 */
void AdaptiveController::initialize(const fec_controller_config_t &config) {
    config_ = config;
    current_profile_ = config.initial_profile;
    downgrade_candidate_ = config.initial_profile;
}

/**
 * @brief 聚合多设备链路指标，并按保持与防抖规则更新建议 profile。
 * @param metrics [in] 链路指标数组；count 非零时不得为空。
 * @param count [in] metrics 数组元素数量。
 * @param now_ms [in] 调用方单调时钟当前毫秒值。
 * @param profile [out] 接收本次决策后的建议 profile。
 * @param changed [out] 接收 profile 是否在本次调用中改变。
 * @return 成功返回 FEC_OK，指标非法返回 FEC_ERR_ARGUMENT。
 */
int AdaptiveController::update(const fec_link_metrics_t *metrics,
                               std::size_t count,
                               uint32_t now_ms,
                               fec_profile_t &profile,
                               int &changed) {
    profile = current_profile_;
    changed = 0;
    if (count == 0u) {
        return FEC_OK;
    }

    uint32_t worst_loss = 0u;
    uint32_t worst_residual = 0u;
    uint16_t worst_burst = 0u;
    uint16_t worst_same_column = 0u;
    for (std::size_t i = 0u; i < count; ++i) {
        if (metrics[i].loss_rate_ppm > 1000000u ||
            metrics[i].residual_loss_ppm > 1000000u) {
            return FEC_ERR_ARGUMENT;
        }
        worst_loss = std::max(worst_loss, metrics[i].loss_rate_ppm);
        worst_residual =
            std::max(worst_residual, metrics[i].residual_loss_ppm);
        worst_burst = std::max(worst_burst, metrics[i].max_burst);
        worst_same_column =
            std::max(worst_same_column, metrics[i].same_column_losses);
    }

    const fec_profile_t desired = choose_profile(
        worst_loss, worst_residual, worst_burst, worst_same_column);
    if (desired == current_profile_) {
        stable_windows_ = 0u;
        downgrade_candidate_ = desired;
        return FEC_OK;
    }
    const uint32_t hold_ms =
        config_.min_hold_ms == 0u ? 1000u : config_.min_hold_ms;
    if (now_ms - last_change_ms_ < hold_ms) {
        return FEC_OK;
    }

    const uint16_t burst_threshold =
        config_.burst_switch_threshold == 0u ?
            6u : config_.burst_switch_threshold;
    const bool pattern_correction = worst_same_column > 0u ||
        worst_residual >= effective_threshold(
            config_.residual_loss_threshold_ppm, 20000u) ||
        worst_burst >= burst_threshold;
    const bool higher_overhead =
        profile_overhead_ppm(desired) > profile_overhead_ppm(current_profile_);
    if (pattern_correction || higher_overhead) {
        apply_profile(desired, now_ms, profile, changed);
        return FEC_OK;
    }

    /* 降低冗余必须连续稳定多个窗口，避免链路临界状态来回切换。 */
    const uint32_t window_ms =
        config_.window_ms == 0u ? 1000u : config_.window_ms;
    if (now_ms - last_window_ms_ < window_ms) {
        return FEC_OK;
    }
    last_window_ms_ = now_ms;
    if (downgrade_candidate_ != desired) {
        downgrade_candidate_ = desired;
        stable_windows_ = 1u;
    } else if (stable_windows_ < 255u) {
        ++stable_windows_;
    }
    const uint8_t required_windows =
        config_.stable_windows_to_degrade == 0u ?
            3u : config_.stable_windows_to_degrade;
    if (stable_windows_ >= required_windows) {
        apply_profile(desired, now_ms, profile, changed);
    }
    return FEC_OK;
}

/**
 * @brief 查询当前建议的编码 profile。
 * @return 当前 profile。
 */
fec_profile_t AdaptiveController::profile() const {
    return current_profile_;
}

/**
 * @brief 根据最差丢包率、残余丢包和突发形态选择目标 profile。
 * @param worst_loss [in] 最差原始丢包率，单位为 ppm。
 * @param worst_residual [in] 最差 FEC 后残余丢包率，单位为 ppm。
 * @param worst_burst [in] 最长连续丢包数。
 * @param worst_same_column [in] XOR 同列多丢包事件数。
 * @return 与当前链路状态匹配的目标 profile。
 */
fec_profile_t AdaptiveController::choose_profile(
    uint32_t worst_loss,
    uint32_t worst_residual,
    uint16_t worst_burst,
    uint16_t worst_same_column) const {
    const uint32_t high =
        effective_threshold(config_.high_loss_threshold_ppm, 150000u);
    const uint32_t medium =
        effective_threshold(config_.medium_loss_threshold_ppm, 50000u);
    const uint32_t low =
        effective_threshold(config_.low_loss_threshold_ppm, 30000u);
    const uint32_t residual = effective_threshold(
        config_.residual_loss_threshold_ppm, 20000u);
    const uint16_t burst_threshold =
        config_.burst_switch_threshold == 0u ?
            6u : config_.burst_switch_threshold;

    if (worst_same_column > 0u || worst_residual >= residual) {
        return FEC_PROFILE_RS_8_6;
    }
    if (worst_burst >= burst_threshold) {
        return FEC_PROFILE_XOR_I_D4_L8;
    }
    if (worst_loss >= high) {
        return FEC_PROFILE_XOR_I_D3_L4;
    }
    if (worst_loss >= medium) {
        return FEC_PROFILE_XOR_I_D4_L4;
    }
    if (worst_loss >= low) {
        return FEC_PROFILE_XOR_I_D5_L4;
    }
    return FEC_PROFILE_XOR_I_D8_L4;
}

/**
 * @brief 应用一次 profile 变化并重置保持时间和稳定窗口状态。
 * @param desired [in] 待应用的目标 profile。
 * @param now_ms [in] 当前单调时钟毫秒值。
 * @param profile [out] 接收已应用的 profile。
 * @param changed [out] 被置为 1，表示本次发生变化。
 */
void AdaptiveController::apply_profile(fec_profile_t desired,
                                       uint32_t now_ms,
                                       fec_profile_t &profile,
                                       int &changed) {
    current_profile_ = desired;
    downgrade_candidate_ = desired;
    last_change_ms_ = now_ms;
    last_window_ms_ = now_ms;
    stable_windows_ = 0u;
    profile = desired;
    changed = 1;
}

} // namespace internal
} // namespace fec

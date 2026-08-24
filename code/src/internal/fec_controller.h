#ifndef FEC_INTERNAL_CONTROLLER_H
#define FEC_INTERNAL_CONTROLLER_H

#include "fec/fec.h"

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

bool controller_config_valid(const fec_controller_config_t &config);

class AdaptiveController {
public:
    AdaptiveController();

    void initialize(const fec_controller_config_t &config);
    int update(const fec_link_metrics_t *metrics,
               std::size_t count,
               uint32_t now_ms,
               fec_profile_t &profile,
               int &changed);
    fec_profile_t profile() const;

private:
    fec_profile_t choose_profile(uint32_t worst_loss,
                                 uint32_t worst_residual,
                                 uint16_t worst_burst,
                                 uint16_t worst_same_column) const;
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

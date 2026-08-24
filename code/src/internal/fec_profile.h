#ifndef FEC_INTERNAL_PROFILE_H
#define FEC_INTERNAL_PROFILE_H

#include "fec/fec.h"

#include <cstdint>

namespace fec {
namespace internal {

/* profile 的规范化参数，供编解码器和算法模块共同使用。 */
struct ProfileParams {
    uint8_t interleave_rows;
    uint8_t interleave_columns;
    uint8_t total_count;
    uint8_t source_count;
    uint8_t repair_count;
    fec_algorithm_t algorithm;
};

bool get_profile_params(fec_profile_t profile, ProfileParams &out);
uint32_t profile_redundancy_ppm(const ProfileParams &params);
const char *profile_name(fec_profile_t profile);

} // namespace internal
} // namespace fec

#endif

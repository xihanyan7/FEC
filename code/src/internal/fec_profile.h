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

/**
 * @brief 查询固定 profile 的规范化参数。
 * @param profile [in] 待查询的 profile；动态 XOR_DX 无法由本重载解析。
 * @param out [out] 成功时接收规范化参数。
 * @return profile 有效且可解析时返回 true，否则返回 false。
 */
bool get_profile_params(fec_profile_t profile, ProfileParams &out);
/**
 * @brief 查询 profile 的规范化参数，并解析 XOR_DX 的动态分组大小。
 * @param profile [in] 待查询的 profile。
 * @param xor_group_size [in] XOR_DX 每组源包数，其他 profile 忽略。
 * @param out [out] 成功时接收规范化参数。
 * @return profile 及动态参数有效时返回 true，否则返回 false。
 */
bool get_profile_params(fec_profile_t profile,
                        uint8_t xor_group_size,
                        ProfileParams &out);
/**
 * @brief 判断算法是否属于普通或交织 XOR 算法族。
 * @param algorithm [in] 待判断的算法枚举值。
 * @return 属于 XOR 算法族返回 true，否则返回 false。
 */
bool is_xor_algorithm(fec_algorithm_t algorithm);
/**
 * @brief 根据源包数和修复包数计算冗余率百万分比。
 * @param params [in] 已规范化的 profile 参数。
 * @return repair_count/source_count 的百万分比；源包数为零时返回零。
 */
uint32_t profile_redundancy_ppm(const ProfileParams &params);
/**
 * @brief 获取 profile 的静态可读名称。
 * @param profile [in] 待查询的 profile 枚举值。
 * @return 静态只读名称；未知值返回 "UNKNOWN"。
 */
const char *profile_name(fec_profile_t profile);

} // namespace internal
} // namespace fec

#endif

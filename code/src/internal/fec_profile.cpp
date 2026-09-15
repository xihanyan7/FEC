#include "internal/fec_profile.h"

#include <cstring>

namespace fec {
namespace internal {

bool get_profile_params(fec_profile_t profile,
                        uint8_t xor_group_size,
                        ProfileParams &out) {
    std::memset(&out, 0, sizeof(out));
    switch (profile) {
    case FEC_PROFILE_NONE:
        out.total_count = 1u;
        out.source_count = 1u;
        out.algorithm = FEC_ALGORITHM_NONE;
        return true;
    case FEC_PROFILE_XOR_I_D8_L4:
        out.interleave_rows = 8u;
        out.interleave_columns = 4u;
        break;
    case FEC_PROFILE_XOR_I_D5_L4:
        out.interleave_rows = 5u;
        out.interleave_columns = 4u;
        break;
    case FEC_PROFILE_XOR_I_D4_L4:
        out.interleave_rows = 4u;
        out.interleave_columns = 4u;
        break;
    case FEC_PROFILE_XOR_I_D3_L4:
        out.interleave_rows = 3u;
        out.interleave_columns = 4u;
        break;
    case FEC_PROFILE_XOR_I_D4_L8:
        out.interleave_rows = 4u;
        out.interleave_columns = 8u;
        break;
    case FEC_PROFILE_RS_8_6:
        out.total_count = 8u;
        out.source_count = 6u;
        out.repair_count = 2u;
        out.algorithm = FEC_ALGORITHM_REED_SOLOMON;
        return true;
    case FEC_PROFILE_RS_10_8:
        out.total_count = 10u;
        out.source_count = 8u;
        out.repair_count = 2u;
        out.algorithm = FEC_ALGORITHM_REED_SOLOMON;
        return true;
    case FEC_PROFILE_XOR_DX:
        if (xor_group_size == 0u || xor_group_size > 32u) {
            return false;
        }
        /* 内部复用单列 XOR 状态机；L=1 时不存在实际交织。 */
        out.interleave_rows = xor_group_size;
        out.interleave_columns = 1u;
        out.source_count = xor_group_size;
        out.repair_count = 1u;
        out.total_count = static_cast<uint8_t>(xor_group_size + 1u);
        out.algorithm = FEC_ALGORITHM_XOR;
        return true;
    default:
        return false;
    }

    out.source_count = static_cast<uint8_t>(
        out.interleave_rows * out.interleave_columns);
    out.repair_count = out.interleave_columns;
    out.total_count = static_cast<uint8_t>(
        out.source_count + out.repair_count);
    out.algorithm = FEC_ALGORITHM_XOR_INTERLEAVED;
    return true;
}

bool get_profile_params(fec_profile_t profile, ProfileParams &out) {
    return get_profile_params(profile, 0u, out);
}

bool is_xor_algorithm(fec_algorithm_t algorithm) {
    return algorithm == FEC_ALGORITHM_XOR_INTERLEAVED ||
           algorithm == FEC_ALGORITHM_XOR;
}

uint32_t profile_redundancy_ppm(const ProfileParams &params) {
    if (params.source_count == 0u) {
        return 0u;
    }
    return static_cast<uint32_t>(params.repair_count) * 1000000u /
           params.source_count;
}

const char *profile_name(fec_profile_t profile) {
    switch (profile) {
    case FEC_PROFILE_NONE: return "NONE";
    case FEC_PROFILE_XOR_I_D8_L4: return "XOR_I_D8_L4";
    case FEC_PROFILE_XOR_I_D5_L4: return "XOR_I_D5_L4";
    case FEC_PROFILE_XOR_I_D4_L4: return "XOR_I_D4_L4";
    case FEC_PROFILE_XOR_I_D3_L4: return "XOR_I_D3_L4";
    case FEC_PROFILE_XOR_I_D4_L8: return "XOR_I_D4_L8";
    case FEC_PROFILE_RS_8_6: return "RS_8_6";
    case FEC_PROFILE_RS_10_8: return "RS_10_8";
    case FEC_PROFILE_XOR_DX: return "XOR_DX";
    case FEC_PROFILE_AUTO: return "AUTO";
    default: return "UNKNOWN";
    }
}

} // namespace internal
} // namespace fec

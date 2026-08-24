#include "fec/fec.h"

#include "internal/fec_constants.h"
#include "internal/fec_controller.h"
#include "internal/fec_decoder.h"
#include "internal/fec_encoder.h"
#include "internal/fec_profile.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

using fec::internal::AdaptiveController;
using fec::internal::Decoder;
using fec::internal::Encoder;
using fec::internal::ProfileParams;

/*
 * 三个公开句柄都是不透明的 C ABI 对象。算法和状态全部由 C++ 类负责，
 * 本文件只处理参数边界、对象生命周期和公开类型转换。
 */
struct fec_encoder {
    Encoder implementation;
};

struct fec_decoder {
    Decoder implementation;
};

struct fec_controller {
    AdaptiveController implementation;
};

namespace {

uint32_t rate_ppm(uint64_t numerator, uint64_t denominator) {
    if (denominator == 0u || numerator == 0u) {
        return 0u;
    }
    if (numerator >= denominator) {
        return 1000000u;
    }
    if (numerator <= std::numeric_limits<uint64_t>::max() / 1000000u) {
        return static_cast<uint32_t>((numerator * 1000000u) / denominator);
    }
    const uint64_t scaled_denominator = denominator / 1000000u;
    if (scaled_denominator == 0u) {
        return 1000000u;
    }
    return static_cast<uint32_t>(std::min<uint64_t>(
        1000000u, numerator / scaled_denominator));
}

bool normalize_config(const fec_config_t &input, fec_config_t &output) {
    if (input.max_packet_size == 0u || input.max_packet_size > 65529u) {
        return false;
    }
    output = input;
    output.symbol_size = input.symbol_size == 0u ?
        static_cast<uint16_t>(input.max_packet_size + 6u) : input.symbol_size;
    return output.symbol_size >=
        static_cast<uint32_t>(input.max_packet_size) + 6u;
}

} // namespace

extern "C" {

int fec_encoder_create(const fec_config_t *config,
                       fec_frame_callback callback,
                       void *user,
                       fec_encoder_t **out_encoder) {
    if (config == NULL || callback == NULL || out_encoder == NULL ||
        config->profile == FEC_PROFILE_AUTO) {
        return FEC_ERR_ARGUMENT;
    }
    *out_encoder = NULL;

    ProfileParams params;
    if (!fec::internal::get_profile_params(config->profile, params)) {
        return FEC_ERR_PROFILE;
    }
    fec_config_t normalized;
    if (!normalize_config(*config, normalized)) {
        return FEC_ERR_ARGUMENT;
    }

    fec_encoder *encoder = new (std::nothrow) fec_encoder;
    if (encoder == NULL) {
        return FEC_ERR_NOMEM;
    }
    const int status = encoder->implementation.initialize(
        normalized, callback, user);
    if (status != FEC_OK) {
        delete encoder;
        return status;
    }
    *out_encoder = encoder;
    return FEC_OK;
}

void fec_encoder_destroy(fec_encoder_t *encoder) {
    delete encoder;
}

int fec_encoder_push(fec_encoder_t *encoder,
                     const uint8_t *data,
                     size_t size) {
    return encoder == NULL ?
        FEC_ERR_ARGUMENT : encoder->implementation.push(data, size);
}

int fec_encoder_flush(fec_encoder_t *encoder) {
    return encoder == NULL ? FEC_ERR_ARGUMENT : encoder->implementation.flush();
}

int fec_encoder_request_profile(fec_encoder_t *encoder, fec_profile_t profile) {
    return encoder == NULL ?
        FEC_ERR_ARGUMENT : encoder->implementation.request_profile(profile);
}

fec_profile_t fec_encoder_get_profile(const fec_encoder_t *encoder) {
    return encoder == NULL ? FEC_PROFILE_NONE : encoder->implementation.profile();
}

int fec_decoder_create(const fec_config_t *config,
                       fec_packet_callback packet_callback,
                       fec_event_callback event_callback,
                       void *user,
                       fec_decoder_t **out_decoder) {
    if (config == NULL || packet_callback == NULL || out_decoder == NULL) {
        return FEC_ERR_ARGUMENT;
    }
    *out_decoder = NULL;

    ProfileParams params;
    if (config->profile != FEC_PROFILE_AUTO &&
        !fec::internal::get_profile_params(config->profile, params)) {
        return FEC_ERR_PROFILE;
    }
    fec_config_t normalized;
    if (!normalize_config(*config, normalized)) {
        return FEC_ERR_ARGUMENT;
    }

    fec_decoder *decoder = new (std::nothrow) fec_decoder;
    if (decoder == NULL) {
        return FEC_ERR_NOMEM;
    }
    const int status = decoder->implementation.initialize(
        normalized, packet_callback, event_callback, user);
    if (status != FEC_OK) {
        delete decoder;
        return status;
    }
    *out_decoder = decoder;
    return FEC_OK;
}

void fec_decoder_destroy(fec_decoder_t *decoder) {
    delete decoder;
}

int fec_decoder_ingest(fec_decoder_t *decoder,
                       const uint8_t *frame,
                       size_t frame_size,
                       uint32_t now_ms) {
    return decoder == NULL ?
        FEC_ERR_ARGUMENT : decoder->implementation.ingest(frame, frame_size, now_ms);
}

int fec_decoder_expire(fec_decoder_t *decoder, uint32_t now_ms) {
    return decoder == NULL ?
        FEC_ERR_ARGUMENT : decoder->implementation.expire(now_ms);
}

int fec_decoder_flush(fec_decoder_t *decoder) {
    return decoder == NULL ? FEC_ERR_ARGUMENT : decoder->implementation.flush();
}

int fec_decoder_reset(fec_decoder_t *decoder) {
    return decoder == NULL ? FEC_ERR_ARGUMENT : decoder->implementation.reset();
}

int fec_decoder_get_stats(fec_decoder_t *decoder,
                          fec_decoder_stats_t *out_stats,
                          int reset_after_read) {
    if (decoder == NULL || out_stats == NULL) {
        return FEC_ERR_ARGUMENT;
    }
    return decoder->implementation.get_stats(*out_stats, reset_after_read != 0);
}

int fec_link_metrics_from_stats(uint32_t device_id,
                                const fec_decoder_stats_t *stats,
                                fec_link_metrics_t *out_metrics) {
    if (stats == NULL || out_metrics == NULL) {
        return FEC_ERR_ARGUMENT;
    }
    std::memset(out_metrics, 0, sizeof(*out_metrics));
    out_metrics->device_id = device_id;
    out_metrics->loss_rate_ppm =
        rate_ppm(stats->raw_missing_packets, stats->source_packets_expected);
    out_metrics->residual_loss_ppm =
        rate_ppm(stats->unrecoverable_packets, stats->source_packets_expected);
    out_metrics->max_burst = static_cast<uint16_t>(
        std::min<uint32_t>(stats->max_missing_burst, 65535u));
    out_metrics->same_column_losses = static_cast<uint16_t>(
        std::min<uint32_t>(stats->same_column_loss_groups, 65535u));
    return FEC_OK;
}

int fec_controller_create(const fec_controller_config_t *config,
                          fec_controller_t **out_controller) {
    if (config == NULL || out_controller == NULL ||
        !fec::internal::controller_config_valid(*config)) {
        return FEC_ERR_ARGUMENT;
    }
    *out_controller = NULL;
    fec_controller *controller = new (std::nothrow) fec_controller;
    if (controller == NULL) {
        return FEC_ERR_NOMEM;
    }
    controller->implementation.initialize(*config);
    *out_controller = controller;
    return FEC_OK;
}

void fec_controller_destroy(fec_controller_t *controller) {
    delete controller;
}

int fec_controller_update(fec_controller_t *controller,
                          const fec_link_metrics_t *metrics,
                          size_t count,
                          uint32_t now_ms,
                          fec_profile_t *profile,
                          int *changed) {
    if (controller == NULL || profile == NULL || changed == NULL ||
        (metrics == NULL && count != 0u)) {
        return FEC_ERR_ARGUMENT;
    }
    return controller->implementation.update(
        metrics, count, now_ms, *profile, *changed);
}

fec_profile_t fec_controller_get_profile(const fec_controller_t *controller) {
    return controller == NULL ?
        FEC_PROFILE_NONE : controller->implementation.profile();
}

int fec_profile_get_info(fec_profile_t profile, fec_profile_info_t *out_info) {
    if (out_info == NULL) {
        return FEC_ERR_ARGUMENT;
    }
    ProfileParams params;
    if (!fec::internal::get_profile_params(profile, params)) {
        return FEC_ERR_PROFILE;
    }
    std::memset(out_info, 0, sizeof(*out_info));
    out_info->algorithm = params.algorithm;
    out_info->source_count = params.source_count;
    out_info->repair_count = params.repair_count;
    out_info->total_count = params.total_count;
    out_info->interleave_rows = params.interleave_rows;
    out_info->interleave_columns = params.interleave_columns;
    out_info->redundancy_ppm =
        fec::internal::profile_redundancy_ppm(params);
    return FEC_OK;
}

const char *fec_profile_name(fec_profile_t profile) {
    return fec::internal::profile_name(profile);
}

size_t fec_frame_header_size(void) {
    return fec::internal::kHeaderSize;
}

} // extern "C"

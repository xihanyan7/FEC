#include "fec/fec.h"
#include <string.h>

static void frame_cb(void *u, const uint8_t *p, size_t n) {
    (void)u;
    (void)p;
    (void)n;
}

static void packet_cb(void *u, uint32_t s, const uint8_t *p, size_t n, int r) {
    (void)u;
    (void)s;
    (void)p;
    (void)n;
    (void)r;
}

int main(void) {
    fec_config_t c;
    fec_encoder_t *e = 0;
    fec_decoder_t *d = 0;
    fec_profile_info_t info;
    fec_decoder_stats_t stats;
    fec_link_metrics_t metrics;
    const uint8_t sample[3] = {1u, 2u, 3u};

    memset(&c, 0, sizeof(c));
    c.profile = FEC_PROFILE_NONE;
    c.max_packet_size = 64u;
    if (fec_encoder_create(&c, frame_cb, 0, &e) != FEC_OK) return 1;
    if (fec_encoder_push(e, sample, sizeof(sample)) != FEC_OK) return 2;
    if (fec_encoder_get_profile(e) != FEC_PROFILE_NONE) return 3;
    if (fec_decoder_create(&c, packet_cb, 0, 0, &d) != FEC_OK) return 4;
    if (fec_decoder_get_stats(d, &stats, 0) != FEC_OK) return 5;
    if (fec_decoder_flush(d) != FEC_OK) return 6;
    if (fec_link_metrics_from_stats(7u, &stats, &metrics) != FEC_OK) return 7;
    if (fec_profile_get_info(FEC_PROFILE_XOR_I_D4_L4, &info) != FEC_OK) return 8;
    if (info.source_count != 16u || info.repair_count != 4u) return 9;
    fec_encoder_destroy(e);
    fec_decoder_destroy(d);
    return fec_frame_header_size() == 32u ? 0 : 10;
}

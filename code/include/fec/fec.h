#ifndef FEC_FEC_H
#define FEC_FEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fec_profile {
    FEC_PROFILE_NONE = 0,
    FEC_PROFILE_XOR_I_D8_L4 = 1,
    FEC_PROFILE_XOR_I_D5_L4 = 2,
    FEC_PROFILE_XOR_I_D4_L4 = 3,
    FEC_PROFILE_XOR_I_D3_L4 = 4,
    FEC_PROFILE_XOR_I_D4_L8 = 5,
    FEC_PROFILE_RS_8_6 = 6,
    FEC_PROFILE_RS_10_8 = 7,
    /* 连续 X 个源包生成 1 个 XOR repair，X 由 fec_config.xor_group_size 指定。 */
    FEC_PROFILE_XOR_DX = 8,
    /* AUTO 仅供解码器使用，编码器必须选择具体 profile。 */
    FEC_PROFILE_AUTO = 255
} fec_profile_t;

typedef enum fec_algorithm {
    FEC_ALGORITHM_NONE = 0,
    FEC_ALGORITHM_XOR_INTERLEAVED = 1,
    FEC_ALGORITHM_REED_SOLOMON = 2,
    FEC_ALGORITHM_XOR = 3
} fec_algorithm_t;

typedef enum fec_status {
    FEC_OK = 0,
    FEC_ERR_ARGUMENT = -1,
    FEC_ERR_NOMEM = -2,
    FEC_ERR_FORMAT = -3,
    FEC_ERR_CRC = -4,
    FEC_ERR_PROFILE = -5,
    FEC_ERR_TOO_LARGE = -6,
    FEC_ERR_BUSY = -7,
    FEC_ERR_NOT_FOUND = -8,
    FEC_ERR_UNRECOVERABLE = -9
} fec_status_t;

typedef enum fec_frame_type {
    FEC_FRAME_SOURCE = 0,
    FEC_FRAME_REPAIR = 1
} fec_frame_type_t;

typedef struct fec_profile_info {
    fec_algorithm_t algorithm;
    uint8_t source_count;
    uint8_t repair_count;
    uint8_t total_count;
    uint8_t interleave_rows;
    uint8_t interleave_columns;
    /* 仅 XOR_DX 有效；表示每个 repair 保护的连续源包数量。 */
    uint8_t xor_group_size;
    uint32_t redundancy_ppm;
} fec_profile_info_t;

typedef struct fec_config {
    fec_profile_t profile;
    uint32_t stream_id;
    uint32_t session_epoch;
    /* 逻辑保护符号大小；0 表示 max_packet_size + 6。 */
    uint16_t symbol_size;
    uint16_t max_packet_size;
    /* XOR_DX 的 X，合法范围 1..32；其他 profile 忽略此字段。 */
    uint8_t xor_group_size;
    /* 解码器活动块上限；0 表示 4。 */
    uint8_t max_active_blocks;
    /* 从块首帧开始计算；0 表示不自动过期。 */
    uint32_t block_timeout_ms;
} fec_config_t;

/*
 * 回调是同步的，不支持背压。frame/data 指针仅在回调期间有效，
 * 回调返回后调用者必须已经发送或复制数据。回调内不得重入同一上下文。
 */
typedef void (*fec_frame_callback)(void *user, const uint8_t *frame, size_t frame_size);
typedef void (*fec_packet_callback)(void *user,
                                    uint32_t source_seq,
                                    const uint8_t *data,
                                    size_t size,
                                    int recovered);
typedef void (*fec_event_callback)(void *user, uint32_t source_seq, int status);

typedef struct fec_encoder fec_encoder_t;
typedef struct fec_decoder fec_decoder_t;
typedef struct fec_controller fec_controller_t;

/* create 阶段分配全部工作区；push/ingest/expire 热路径不分配堆内存。 */
int fec_encoder_create(const fec_config_t *config,
                       fec_frame_callback callback,
                       void *user,
                       fec_encoder_t **out_encoder);
void fec_encoder_destroy(fec_encoder_t *encoder);
int fec_encoder_push(fec_encoder_t *encoder, const uint8_t *data, size_t size);
int fec_encoder_flush(fec_encoder_t *encoder);
/* 请求在下一个源块边界切换，绝不会改变当前未完成块。 */
int fec_encoder_request_profile(fec_encoder_t *encoder, fec_profile_t profile);
fec_profile_t fec_encoder_get_profile(const fec_encoder_t *encoder);

int fec_decoder_create(const fec_config_t *config,
                       fec_packet_callback packet_callback,
                       fec_event_callback event_callback,
                       void *user,
                       fec_decoder_t **out_decoder);
void fec_decoder_destroy(fec_decoder_t *decoder);
int fec_decoder_ingest(fec_decoder_t *decoder,
                       const uint8_t *frame,
                       size_t frame_size,
                       uint32_t now_ms);
int fec_decoder_expire(fec_decoder_t *decoder, uint32_t now_ms);
/* 结束流并关闭所有活动块；不足恢复条件的源包触发 event callback。 */
int fec_decoder_flush(fec_decoder_t *decoder);
/* reset 清空活动块和迟到包记录，但不清空累计统计。 */
int fec_decoder_reset(fec_decoder_t *decoder);

typedef struct fec_decoder_stats {
    uint64_t valid_frames;
    uint64_t source_frames;
    uint64_t repair_frames;
    uint64_t duplicate_or_late_frames;
    uint64_t recovered_packets;
    uint64_t source_packets_expected;
    uint64_t raw_missing_packets;
    uint64_t unrecoverable_packets;
    uint64_t completed_blocks;
    uint64_t expired_blocks;
    uint64_t evicted_blocks;
    uint64_t crc_errors;
    uint64_t format_errors;
    uint64_t profile_errors;
    uint64_t recovery_crc_errors;
    uint32_t max_missing_burst;
    uint32_t same_column_loss_groups;
} fec_decoder_stats_t;

/* reset_after_read 非零时，在复制快照后清零统计窗口。 */
int fec_decoder_get_stats(fec_decoder_t *decoder,
                          fec_decoder_stats_t *out_stats,
                          int reset_after_read);

typedef struct fec_link_metrics {
    uint32_t device_id;
    /* 取值 0..1000000，表示百万分比。 */
    uint32_t loss_rate_ppm;
    uint16_t max_burst;
    /* 统计窗口内出现“同一 XOR 列至少两个源包缺失”的列数。 */
    uint16_t same_column_losses;
    uint32_t residual_loss_ppm;
} fec_link_metrics_t;

/* 把一个解码统计窗口转换成控制器输入。 */
int fec_link_metrics_from_stats(uint32_t device_id,
                                const fec_decoder_stats_t *stats,
                                fec_link_metrics_t *out_metrics);

typedef struct fec_controller_config {
    fec_profile_t initial_profile;
    uint32_t min_hold_ms;
    uint8_t stable_windows_to_degrade;
    uint32_t window_ms;
    uint32_t high_loss_threshold_ppm;       /* 默认 150000 */
    uint32_t medium_loss_threshold_ppm;     /* 默认 50000 */
    uint32_t low_loss_threshold_ppm;        /* 默认 30000 */
    uint32_t residual_loss_threshold_ppm;   /* 默认 20000 */
    uint16_t burst_switch_threshold;        /* 默认 6 */
} fec_controller_config_t;

int fec_controller_create(const fec_controller_config_t *config,
                          fec_controller_t **out_controller);
void fec_controller_destroy(fec_controller_t *controller);
/* 多设备指标按最差设备聚合；changed 只表示“建议 profile 已改变”。 */
int fec_controller_update(fec_controller_t *controller,
                          const fec_link_metrics_t *metrics,
                          size_t count,
                          uint32_t now_ms,
                          fec_profile_t *profile,
                          int *changed);
fec_profile_t fec_controller_get_profile(const fec_controller_t *controller);

int fec_profile_get_info(fec_profile_t profile, fec_profile_info_t *out_info);
/* 动态 profile 应使用本接口，以便把配置中的 X 解析为完整参数。 */
int fec_config_get_profile_info(const fec_config_t *config,
                                fec_profile_info_t *out_info);
const char *fec_profile_name(fec_profile_t profile);
size_t fec_frame_header_size(void);

#ifdef __cplusplus
}
#endif

#endif

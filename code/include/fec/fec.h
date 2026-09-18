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
 * 所有回调均为同步回调且不支持背压。frame/data 指针仅在回调期间有效，
 * 回调返回前调用者必须完成发送或复制。回调内不得重入触发它的同一上下文。
 */
/**
 * @brief 接收编码器产生的 FEC SOURCE 或 REPAIR 帧。
 * @param user [in,out] 创建编码器时传入的用户上下文。
 * @param frame [in] FEC 帧首地址，仅在本次回调期间有效。
 * @param frame_size [in] FEC 帧长度，单位为字节。
 */
typedef void (*fec_frame_callback)(void *user, const uint8_t *frame, size_t frame_size);
/**
 * @brief 接收解码器交付的原始上层包或恢复包。
 * @param user [in,out] 创建解码器时传入的用户上下文。
 * @param source_seq [in] FEC 为原始包分配的连续源包序号。
 * @param data [in] 原始包首地址，仅在本次回调期间有效。
 * @param size [in] 原始包长度，单位为字节。
 * @param recovered [in] 非零表示该包由 FEC 恢复，零表示直接取自 SOURCE 帧。
 */
typedef void (*fec_packet_callback)(void *user,
                                    uint32_t source_seq,
                                    const uint8_t *data,
                                    size_t size,
                                    int recovered);
/**
 * @brief 通知某个源包最终无法恢复等解码事件。
 * @param user [in,out] 创建解码器时传入的用户上下文。
 * @param source_seq [in] 事件对应的源包序号。
 * @param status [in] 事件状态码，取值见 fec_status_t。
 */
typedef void (*fec_event_callback)(void *user, uint32_t source_seq, int status);

typedef struct fec_encoder fec_encoder_t;
typedef struct fec_decoder fec_decoder_t;
typedef struct fec_controller fec_controller_t;

/**
 * @brief 创建并初始化 FEC 编码器，在此阶段一次性分配全部工作区。
 * @param config [in] 编码参数；函数返回后可释放该结构。
 * @param callback [in] 编码帧同步输出回调，不得为空。
 * @param user [in,out] 原样传递给 callback 的用户上下文，可为空。
 * @param out_encoder [out] 成功时接收编码器句柄，失败时置为空。
 * @return FEC_OK 表示成功，否则返回 fec_status_t 中的错误码。
 */
int fec_encoder_create(const fec_config_t *config,
                       fec_frame_callback callback,
                       void *user,
                       fec_encoder_t **out_encoder);
/**
 * @brief 销毁编码器并释放其全部资源；传入空指针是安全的。
 * @param encoder [in] 待销毁的编码器句柄，可为空。
 */
void fec_encoder_destroy(fec_encoder_t *encoder);
/**
 * @brief 向编码器提交一个完整的原始上层包，并同步输出 SOURCE/REPAIR 帧。
 * @param encoder [in,out] 有效的编码器句柄。
 * @param data [in] 原始包首地址；size 非零时不得为空。
 * @param size [in] 原始包长度，不得超过配置的 max_packet_size。
 * @return FEC_OK 表示成功，否则返回参数、长度或编码错误码。
 */
int fec_encoder_push(fec_encoder_t *encoder, const uint8_t *data, size_t size);
/**
 * @brief 丢弃当前未完成块的修复状态，并让后续输入从新块开始。
 * @param encoder [in,out] 有效的编码器句柄。
 * @return FEC_OK 表示成功，FEC_ERR_ARGUMENT 表示句柄为空。
 */
int fec_encoder_flush(fec_encoder_t *encoder);
/**
 * @brief 请求在下一个源块边界切换编码 profile，不改变当前未完成块。
 * @param encoder [in,out] 有效的编码器句柄。
 * @param profile [in] 目标编码 profile；不得为 FEC_PROFILE_AUTO。
 * @return FEC_OK 表示请求已接受，否则返回参数或 profile 错误码。
 */
int fec_encoder_request_profile(fec_encoder_t *encoder, fec_profile_t profile);
/**
 * @brief 查询编码器当前实际使用的 profile。
 * @param encoder [in] 有效的编码器句柄。
 * @return 当前 profile；句柄为空时返回 FEC_PROFILE_NONE。
 */
fec_profile_t fec_encoder_get_profile(const fec_encoder_t *encoder);

/**
 * @brief 创建并初始化 FEC 解码器，在此阶段一次性分配全部工作区。
 * @param config [in] 解码参数；profile 可设为 FEC_PROFILE_AUTO。
 * @param packet_callback [in] 原始包同步输出回调，不得为空。
 * @param event_callback [in] 解码事件同步回调，可为空。
 * @param user [in,out] 原样传递给两个回调的用户上下文，可为空。
 * @param out_decoder [out] 成功时接收解码器句柄，失败时置为空。
 * @return FEC_OK 表示成功，否则返回 fec_status_t 中的错误码。
 */
int fec_decoder_create(const fec_config_t *config,
                       fec_packet_callback packet_callback,
                       fec_event_callback event_callback,
                       void *user,
                       fec_decoder_t **out_decoder);
/**
 * @brief 销毁解码器并释放其全部资源；传入空指针是安全的。
 * @param decoder [in] 待销毁的解码器句柄，可为空。
 */
void fec_decoder_destroy(fec_decoder_t *decoder);
/**
 * @brief 输入一个完整 FEC 帧，完成校验、去重、缓存及可能的恢复输出。
 * @param decoder [in,out] 有效的解码器句柄。
 * @param frame [in] 完整 FEC SOURCE 或 REPAIR 帧首地址。
 * @param frame_size [in] 帧长度，单位为字节。
 * @param now_ms [in] 调用方单调时钟的当前毫秒值，用于块超时判断。
 * @return FEC_OK 表示帧已处理，否则返回格式、CRC、profile 等错误码。
 */
int fec_decoder_ingest(fec_decoder_t *decoder,
                       const uint8_t *frame,
                       size_t frame_size,
                       uint32_t now_ms);
/**
 * @brief 按当前时间淘汰已超过 block_timeout_ms 的活动块。
 * @param decoder [in,out] 有效的解码器句柄。
 * @param now_ms [in] 与 ingest 使用相同时间基准的当前毫秒值。
 * @return FEC_OK 表示处理完成，FEC_ERR_ARGUMENT 表示句柄为空。
 */
int fec_decoder_expire(fec_decoder_t *decoder, uint32_t now_ms);
/**
 * @brief 结束当前流并关闭所有活动块，不可恢复的源包会触发事件回调。
 * @param decoder [in,out] 有效的解码器句柄。
 * @return FEC_OK 表示处理完成，FEC_ERR_ARGUMENT 表示句柄为空。
 */
int fec_decoder_flush(fec_decoder_t *decoder);
/**
 * @brief 清空活动块和迟到包记录，但保留累计统计数据。
 * @param decoder [in,out] 有效的解码器句柄。
 * @return FEC_OK 表示重置完成，FEC_ERR_ARGUMENT 表示句柄为空。
 */
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

/**
 * @brief 获取解码统计快照，并可在读取后清零内部统计窗口。
 * @param decoder [in,out] 有效的解码器句柄。
 * @param out_stats [out] 接收完整统计快照，不得为空。
 * @param reset_after_read [in] 非零时复制快照后清零内部统计数据。
 * @return FEC_OK 表示成功，FEC_ERR_ARGUMENT 表示参数无效。
 */
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

/**
 * @brief 将一个解码统计窗口转换成自适应控制器可用的链路指标。
 * @param device_id [in] 指标所属设备标识，由调用方定义。
 * @param stats [in] 解码器统计快照，不得为空。
 * @param out_metrics [out] 接收换算后的丢包率、残余丢包率和突发指标。
 * @return FEC_OK 表示成功，FEC_ERR_ARGUMENT 表示参数为空。
 */
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

/**
 * @brief 创建自适应 profile 控制器。
 * @param config [in] 控制阈值及初始 profile 配置，不得为空。
 * @param out_controller [out] 成功时接收控制器句柄，失败时置为空。
 * @return FEC_OK 表示成功，否则返回参数、配置或内存错误码。
 */
int fec_controller_create(const fec_controller_config_t *config,
                          fec_controller_t **out_controller);
/**
 * @brief 销毁自适应控制器；传入空指针是安全的。
 * @param controller [in] 待销毁的控制器句柄，可为空。
 */
void fec_controller_destroy(fec_controller_t *controller);
/**
 * @brief 根据一个或多个设备的链路指标更新建议 profile。
 * @param controller [in,out] 有效的控制器句柄。
 * @param metrics [in] 指标数组；count 非零时不得为空。
 * @param count [in] metrics 数组元素个数；多设备按最差指标聚合。
 * @param now_ms [in] 调用方单调时钟的当前毫秒值。
 * @param profile [out] 接收本次更新后的建议 profile。
 * @param changed [out] 接收变更标志；非零仅表示建议 profile 已改变。
 * @return FEC_OK 表示成功，否则返回参数或控制状态错误码。
 */
int fec_controller_update(fec_controller_t *controller,
                          const fec_link_metrics_t *metrics,
                          size_t count,
                          uint32_t now_ms,
                          fec_profile_t *profile,
                          int *changed);
/**
 * @brief 查询控制器当前建议的 profile。
 * @param controller [in] 有效的控制器句柄。
 * @return 当前建议 profile；句柄为空时返回 FEC_PROFILE_NONE。
 */
fec_profile_t fec_controller_get_profile(const fec_controller_t *controller);

/**
 * @brief 查询固定 profile 的规范化算法参数。
 * @param profile [in] 待查询的固定 profile；不支持动态 XOR_DX 和 AUTO。
 * @param out_info [out] 接收算法、源包数、修复包数和冗余率等信息。
 * @return FEC_OK 表示成功，否则返回参数或 profile 错误码。
 */
int fec_profile_get_info(fec_profile_t profile, fec_profile_info_t *out_info);
/**
 * @brief 根据完整配置查询 profile 参数，可解析动态 XOR_DX 的 X 值。
 * @param config [in] 包含 profile 及动态参数的 FEC 配置。
 * @param out_info [out] 接收规范化后的 profile 信息。
 * @return FEC_OK 表示成功，否则返回参数或 profile 错误码。
 */
int fec_config_get_profile_info(const fec_config_t *config,
                                fec_profile_info_t *out_info);
/**
 * @brief 获取 profile 的静态可读名称。
 * @param profile [in] 待查询的 profile 枚举值。
 * @return 指向静态只读字符串的指针；未知值返回 "UNKNOWN"。
 */
const char *fec_profile_name(fec_profile_t profile);
/**
 * @brief 获取当前线格式固定 FEC 帧头的字节数。
 * @return FEC 帧头长度，单位为字节。
 */
size_t fec_frame_header_size(void);

#ifdef __cplusplus
}
#endif

#endif

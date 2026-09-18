#ifndef FEC_INTERNAL_FRAME_H
#define FEC_INTERNAL_FRAME_H

#include "fec/fec.h"

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

struct FrameFields {
    fec_profile_t profile;
    fec_frame_type_t type;
    uint8_t xor_group_size;
    uint32_t stream_id;
    uint32_t session_epoch;
    uint32_t block_id;
    uint32_t base_seq;
    uint16_t index;
    uint16_t symbol_size;
};

/* ParsedFrame 只引用调用方输入缓冲区，解析过程不复制 payload。 */
struct ParsedFrame : FrameFields {
    const uint8_t *payload;
    uint16_t payload_size;
    uint32_t payload_crc;
};

class FrameCodec {
public:
    /**
     * @brief 将帧字段和 payload 序列化为带头部 CRC 的完整 FEC 帧。
     * @param buffer [out] 接收完整帧的输出缓冲区。
     * @param capacity [in] buffer 的可写容量，单位为字节。
     * @param fields [in] profile、流、块和索引等帧头字段。
     * @param payload [in] 待封装 payload；payload_size 非零时不得为空。
     * @param payload_size [in] payload 长度，必须能用线格式长度字段表示。
     * @param frame_size [out] 成功时接收最终帧长度。
     * @return FEC_OK 表示成功，否则返回参数或容量错误码。
     */
    static int write(uint8_t *buffer,
                     std::size_t capacity,
                     const FrameFields &fields,
                     const uint8_t *payload,
                     std::size_t payload_size,
                     std::size_t &frame_size);

    /**
     * @brief 校验并解析完整 FEC 帧；解析结果直接引用输入缓冲区。
     * @param frame [in] 完整 FEC 帧首地址。
     * @param frame_size [in] 输入帧长度，单位为字节。
     * @param out [out] 接收解析出的帧字段和 payload 视图。
     * @return FEC_OK 表示成功，否则返回参数、格式或 CRC 错误码。
     */
    static int parse(const uint8_t *frame,
                     std::size_t frame_size,
                     ParsedFrame &out);

    /**
     * @brief 计算一段字节数据的 IEEE CRC32。
     * @param data [in] 输入数据首地址；size 非零时不得为空。
     * @param size [in] 输入数据长度，单位为字节。
     * @return 计算得到的 32 位 CRC 值。
     */
    static uint32_t crc32(const uint8_t *data, std::size_t size);
};

} // namespace internal
} // namespace fec

#endif

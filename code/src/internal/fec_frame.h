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
    static int write(uint8_t *buffer,
                     std::size_t capacity,
                     const FrameFields &fields,
                     const uint8_t *payload,
                     std::size_t payload_size,
                     std::size_t &frame_size);

    static int parse(const uint8_t *frame,
                     std::size_t frame_size,
                     ParsedFrame &out);

    static uint32_t crc32(const uint8_t *data, std::size_t size);
};

} // namespace internal
} // namespace fec

#endif

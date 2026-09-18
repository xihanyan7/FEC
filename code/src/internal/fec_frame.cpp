#include "internal/fec_frame.h"

#include "internal/fec_constants.h"

#include <cstring>

namespace fec {
namespace internal {
namespace {

/**
 * @brief 按线格式小端序写入 16 位无符号整数。
 * @param out [out] 至少可写 2 字节的目标地址。
 * @param value [in] 待写入数值。
 */
void put_u16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
}

/**
 * @brief 按线格式小端序写入 32 位无符号整数。
 * @param out [out] 至少可写 4 字节的目标地址。
 * @param value [in] 待写入数值。
 */
void put_u32(uint8_t *out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
    out[2] = static_cast<uint8_t>(value >> 16u);
    out[3] = static_cast<uint8_t>(value >> 24u);
}

/**
 * @brief 从线格式小端字节序读取 16 位无符号整数。
 * @param in [in] 至少包含 2 字节的输入地址。
 * @return 解码后的主机整数。
 */
uint16_t get_u16(const uint8_t *in) {
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8u);
}

/**
 * @brief 从线格式小端字节序读取 32 位无符号整数。
 * @param in [in] 至少包含 4 字节的输入地址。
 * @return 解码后的主机整数。
 */
uint32_t get_u32(const uint8_t *in) {
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8u) |
           (static_cast<uint32_t>(in[2]) << 16u) |
           (static_cast<uint32_t>(in[3]) << 24u);
}

} // namespace

uint32_t FrameCodec::crc32(const uint8_t *data, std::size_t size) {
    /* 半字节查表只占 64 字节只读空间，适合资源受限设备。 */
    static const uint32_t table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
    };
    uint32_t value = 0xFFFFFFFFu;
    for (std::size_t i = 0u; i < size; ++i) {
        value ^= data[i];
        value = (value >> 4u) ^ table[value & 0x0Fu];
        value = (value >> 4u) ^ table[value & 0x0Fu];
    }
    return value ^ 0xFFFFFFFFu;
}

int FrameCodec::write(uint8_t *buffer,
                      std::size_t capacity,
                      const FrameFields &fields,
                      const uint8_t *payload,
                      std::size_t payload_size,
                      std::size_t &frame_size) {
    if (buffer == nullptr || (payload == nullptr && payload_size != 0u)) {
        return FEC_ERR_ARGUMENT;
    }
    if (payload_size > 65535u || capacity < kHeaderSize + payload_size + 4u) {
        return FEC_ERR_TOO_LARGE;
    }

    std::memset(buffer, 0, kHeaderSize);
    put_u16(buffer, kMagic);
    buffer[2] = kVersion;
    buffer[3] = static_cast<uint8_t>(fields.profile);
    buffer[4] = static_cast<uint8_t>(fields.type);
    buffer[5] = fields.xor_group_size;
    put_u32(buffer + 6u, fields.stream_id);
    put_u32(buffer + 10u, fields.session_epoch);
    put_u32(buffer + 14u, fields.block_id);
    put_u32(buffer + 18u, fields.base_seq);
    put_u16(buffer + 22u, fields.index);
    put_u16(buffer + 24u, fields.symbol_size);
    put_u16(buffer + 26u, static_cast<uint16_t>(payload_size));
    put_u32(buffer + 28u, crc32(buffer, 28u));
    if (payload_size != 0u) {
        std::memcpy(buffer + kHeaderSize, payload, payload_size);
    }
    put_u32(buffer + kHeaderSize + payload_size, crc32(payload, payload_size));
    frame_size = kHeaderSize + payload_size + 4u;
    return FEC_OK;
}

int FrameCodec::parse(const uint8_t *frame,
                      std::size_t frame_size,
                      ParsedFrame &out) {
    if (frame == nullptr) {
        return FEC_ERR_ARGUMENT;
    }
    if (frame_size < kHeaderSize + 4u || get_u16(frame) != kMagic ||
        frame[2] != kVersion) {
        return FEC_ERR_FORMAT;
    }
    if (crc32(frame, 28u) != get_u32(frame + 28u)) {
        return FEC_ERR_CRC;
    }
    const uint16_t payload_size = get_u16(frame + 26u);
    if (frame_size != kHeaderSize + static_cast<std::size_t>(payload_size) + 4u) {
        return FEC_ERR_FORMAT;
    }
    const uint8_t *payload = frame + kHeaderSize;
    const uint32_t payload_crc = get_u32(payload + payload_size);
    if (crc32(payload, payload_size) != payload_crc) {
        return FEC_ERR_CRC;
    }

    out.profile = static_cast<fec_profile_t>(frame[3]);
    out.type = static_cast<fec_frame_type_t>(frame[4]);
    out.xor_group_size = frame[5];
    out.stream_id = get_u32(frame + 6u);
    out.session_epoch = get_u32(frame + 10u);
    out.block_id = get_u32(frame + 14u);
    out.base_seq = get_u32(frame + 18u);
    out.index = get_u16(frame + 22u);
    out.symbol_size = get_u16(frame + 24u);
    out.payload = payload;
    out.payload_size = payload_size;
    out.payload_crc = payload_crc;
    return FEC_OK;
}

} // namespace internal
} // namespace fec

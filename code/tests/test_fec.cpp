#include "fec/fec.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {

typedef std::vector<uint8_t> Bytes;

struct Wire {
    std::vector<Bytes> frames;
};

struct Received {
    std::map<uint32_t, Bytes> packets;
    std::set<uint32_t> recovered;
    std::vector<uint32_t> unrecoverable;
};

void frame_callback(void *user, const uint8_t *frame, size_t size) {
    Wire *wire = static_cast<Wire *>(user);
    wire->frames.push_back(Bytes(frame, frame + size));
}

void packet_callback(void *user,
                     uint32_t source_seq,
                     const uint8_t *data,
                     size_t size,
                     int recovered) {
    Received *received = static_cast<Received *>(user);
    received->packets[source_seq] = Bytes(data, data + size);
    if (recovered != 0) {
        received->recovered.insert(source_seq);
    }
}

void event_callback(void *user, uint32_t source_seq, int status) {
    Received *received = static_cast<Received *>(user);
    assert(status == FEC_ERR_UNRECOVERABLE);
    received->unrecoverable.push_back(source_seq);
}

fec_config_t make_config(fec_profile_t profile) {
    fec_config_t config;
    std::memset(&config, 0, sizeof(config));
    config.profile = profile;
    config.stream_id = 0x10203040u;
    config.session_epoch = 0x55667788u;
    config.max_packet_size = 97u;
    config.block_timeout_ms = 100u;
    config.max_active_blocks = 4u;
    return config;
}

uint16_t read_u16(const Bytes &frame, size_t offset) {
    return static_cast<uint16_t>(frame[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(frame[offset + 1u]) << 8u);
}

uint32_t read_u32(const Bytes &frame, size_t offset) {
    return static_cast<uint32_t>(frame[offset]) |
           (static_cast<uint32_t>(frame[offset + 1u]) << 8u) |
           (static_cast<uint32_t>(frame[offset + 2u]) << 16u) |
           (static_cast<uint32_t>(frame[offset + 3u]) << 24u);
}

void write_u16(Bytes &frame, size_t offset, uint16_t value) {
    frame[offset] = static_cast<uint8_t>(value);
    frame[offset + 1u] = static_cast<uint8_t>(value >> 8u);
}

void write_u32(Bytes &frame, size_t offset, uint32_t value) {
    frame[offset] = static_cast<uint8_t>(value);
    frame[offset + 1u] = static_cast<uint8_t>(value >> 8u);
    frame[offset + 2u] = static_cast<uint8_t>(value >> 16u);
    frame[offset + 3u] = static_cast<uint8_t>(value >> 24u);
}

uint32_t test_crc32(const uint8_t *data, size_t size) {
    static const uint32_t table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
    };
    uint32_t value = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; ++i) {
        value ^= data[i];
        value = (value >> 4u) ^ table[value & 0x0Fu];
        value = (value >> 4u) ^ table[value & 0x0Fu];
    }
    return value ^ 0xFFFFFFFFu;
}

void refresh_header_crc(Bytes &frame) {
    write_u32(frame, 28u, test_crc32(&frame[0], 28u));
}

std::vector<Bytes> make_packets(uint8_t count) {
    std::vector<Bytes> packets;
    for (uint8_t index = 0u; index < count; ++index) {
        const size_t size = index == 0u ? 0u :
            static_cast<size_t>((index * 19u) % 83u + 1u);
        Bytes packet(size);
        for (size_t offset = 0u; offset < size; ++offset) {
            packet[offset] = static_cast<uint8_t>(index * 37u + offset * 11u);
        }
        packets.push_back(packet);
    }
    return packets;
}

Wire encode_one_block(fec_profile_t profile, std::vector<Bytes> &packets) {
    fec_profile_info_t info;
    assert(fec_profile_get_info(profile, &info) == FEC_OK);
    packets = make_packets(info.source_count);

    const fec_config_t config = make_config(profile);
    Wire wire;
    fec_encoder_t *encoder = NULL;
    assert(fec_encoder_create(&config, frame_callback, &wire, &encoder) == FEC_OK);
    for (size_t i = 0u; i < packets.size(); ++i) {
        const uint8_t *data = packets[i].empty() ? NULL : &packets[i][0];
        assert(fec_encoder_push(encoder, data, packets[i].size()) == FEC_OK);
    }
    fec_encoder_destroy(encoder);
    assert(wire.frames.size() == info.total_count);

    for (size_t i = 0u; i < wire.frames.size(); ++i) {
        assert(read_u16(wire.frames[i], 22u) == i);
        assert(wire.frames[i][3] == static_cast<uint8_t>(profile));
        assert(read_u32(wire.frames[i], 14u) == 0u);
        assert(read_u32(wire.frames[i], 18u) == 0u);
    }
    return wire;
}

struct DecodeResult {
    Received received;
    fec_decoder_stats_t stats;
};

DecodeResult decode_frames(fec_profile_t decoder_profile,
                           const Wire &wire,
                           const std::set<size_t> &drop_positions,
                           bool repair_first,
                           bool duplicate_first) {
    fec_config_t config = make_config(decoder_profile);
    Received received;
    fec_decoder_t *decoder = NULL;
    assert(fec_decoder_create(&config,
                              packet_callback,
                              event_callback,
                              &received,
                              &decoder) == FEC_OK);

    std::vector<size_t> order;
    if (repair_first) {
        for (size_t i = 0u; i < wire.frames.size(); ++i) {
            if (wire.frames[i][4] == FEC_FRAME_REPAIR) {
                order.push_back(i);
            }
        }
        for (size_t i = 0u; i < wire.frames.size(); ++i) {
            if (wire.frames[i][4] == FEC_FRAME_SOURCE) {
                order.push_back(i);
            }
        }
    } else {
        for (size_t i = 0u; i < wire.frames.size(); ++i) {
            order.push_back(i);
        }
    }

    bool duplicated = false;
    for (size_t i = 0u; i < order.size(); ++i) {
        const size_t position = order[i];
        if (drop_positions.count(position) != 0u) {
            continue;
        }
        const Bytes &frame = wire.frames[position];
        assert(fec_decoder_ingest(decoder, &frame[0], frame.size(), 1u) == FEC_OK);
        if (duplicate_first && !duplicated) {
            assert(fec_decoder_ingest(decoder, &frame[0], frame.size(), 2u) == FEC_OK);
            duplicated = true;
        }
    }
    assert(fec_decoder_expire(decoder, 200u) == FEC_OK);

    DecodeResult result;
    result.received = received;
    std::memset(&result.stats, 0, sizeof(result.stats));
    assert(fec_decoder_get_stats(decoder, &result.stats, 0) == FEC_OK);
    fec_decoder_destroy(decoder);
    return result;
}

void assert_packets_equal(const std::vector<Bytes> &expected,
                          const Received &received) {
    assert(received.packets.size() == expected.size());
    for (size_t i = 0u; i < expected.size(); ++i) {
        assert(received.packets.count(static_cast<uint32_t>(i)) == 1u);
        assert(received.packets.find(static_cast<uint32_t>(i))->second == expected[i]);
    }
    assert(received.unrecoverable.empty());
}

void test_profile_metadata() {
    fec_profile_info_t info;
    assert(fec_profile_get_info(FEC_PROFILE_XOR_I_D4_L4, &info) == FEC_OK);
    assert(info.algorithm == FEC_ALGORITHM_XOR_INTERLEAVED);
    assert(info.source_count == 16u && info.repair_count == 4u);
    assert(info.total_count == 20u && info.redundancy_ppm == 250000u);
    assert(fec_profile_get_info(FEC_PROFILE_RS_10_8, &info) == FEC_OK);
    assert(info.algorithm == FEC_ALGORITHM_REED_SOLOMON);
    assert(info.source_count == 8u && info.repair_count == 2u);
    assert(fec_profile_get_info(FEC_PROFILE_AUTO, &info) == FEC_ERR_PROFILE);
}

void test_crc_standard_vector() {
    const fec_config_t config = make_config(FEC_PROFILE_NONE);
    const uint8_t sample[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    Wire wire;
    fec_encoder_t *encoder = NULL;
    assert(fec_encoder_create(&config, frame_callback, &wire, &encoder) == FEC_OK);
    assert(fec_encoder_push(encoder, sample, sizeof(sample)) == FEC_OK);
    fec_encoder_destroy(encoder);
    assert(wire.frames.size() == 1u);
    assert(read_u32(wire.frames[0], fec_frame_header_size() + sizeof(sample)) ==
           0xCBF43926u);
}

void test_all_profiles_single_erasure() {
    const fec_profile_t profiles[] = {
        FEC_PROFILE_NONE,
        FEC_PROFILE_XOR_I_D8_L4,
        FEC_PROFILE_XOR_I_D5_L4,
        FEC_PROFILE_XOR_I_D4_L4,
        FEC_PROFILE_XOR_I_D3_L4,
        FEC_PROFILE_XOR_I_D4_L8,
        FEC_PROFILE_RS_8_6,
        FEC_PROFILE_RS_10_8
    };

    for (size_t p = 0u; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        std::vector<Bytes> packets;
        const Wire wire = encode_one_block(profiles[p], packets);
        assert_packets_equal(packets,
                             decode_frames(profiles[p], wire, std::set<size_t>(), false, true)
                                 .received);
        if (profiles[p] == FEC_PROFILE_NONE) {
            continue;
        }
        for (size_t lost = 0u; lost < wire.frames.size(); ++lost) {
            const DecodeResult result = decode_frames(
                profiles[p], wire, std::set<size_t>{lost}, false, false);
            assert_packets_equal(packets, result.received);
        }
    }
}

void test_xor_bursts_and_same_column_failure() {
    const fec_profile_t profiles[] = {
        FEC_PROFILE_XOR_I_D8_L4,
        FEC_PROFILE_XOR_I_D5_L4,
        FEC_PROFILE_XOR_I_D4_L4,
        FEC_PROFILE_XOR_I_D3_L4,
        FEC_PROFILE_XOR_I_D4_L8
    };

    for (size_t p = 0u; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        fec_profile_info_t info;
        assert(fec_profile_get_info(profiles[p], &info) == FEC_OK);
        std::vector<Bytes> packets;
        const Wire wire = encode_one_block(profiles[p], packets);
        for (size_t burst = 1u; burst <= info.interleave_columns; ++burst) {
            for (size_t start = 0u; start + burst <= wire.frames.size(); ++start) {
                std::set<size_t> drops;
                for (size_t i = 0u; i < burst; ++i) {
                    drops.insert(start + i);
                }
                const DecodeResult result =
                    decode_frames(profiles[p], wire, drops, false, false);
                assert_packets_equal(packets, result.received);
            }
        }
    }

    std::vector<Bytes> packets;
    const Wire wire = encode_one_block(FEC_PROFILE_XOR_I_D4_L4, packets);
    const DecodeResult failed = decode_frames(
        FEC_PROFILE_XOR_I_D4_L4, wire, std::set<size_t>{0u, 4u}, false, false);
    assert(failed.received.packets.size() == packets.size() - 2u);
    assert(failed.received.unrecoverable.size() == 2u);
    assert(failed.stats.raw_missing_packets == 2u);
    assert(failed.stats.unrecoverable_packets == 2u);
    assert(failed.stats.same_column_loss_groups == 1u);

    fec_link_metrics_t metrics;
    assert(fec_link_metrics_from_stats(9u, &failed.stats, &metrics) == FEC_OK);
    assert(metrics.device_id == 9u);
    assert(metrics.same_column_losses == 1u);
    assert(metrics.loss_rate_ppm == 125000u);
    assert(metrics.residual_loss_ppm == 125000u);
}

void test_rs_all_double_erasures() {
    const fec_profile_t profiles[] = {FEC_PROFILE_RS_8_6, FEC_PROFILE_RS_10_8};
    for (size_t p = 0u; p < sizeof(profiles) / sizeof(profiles[0]); ++p) {
        std::vector<Bytes> packets;
        const Wire wire = encode_one_block(profiles[p], packets);
        for (size_t first = 0u; first < wire.frames.size(); ++first) {
            for (size_t second = first + 1u; second < wire.frames.size(); ++second) {
                const DecodeResult result = decode_frames(
                    profiles[p], wire, std::set<size_t>{first, second}, false, false);
                assert_packets_equal(packets, result.received);
            }
        }

        const DecodeResult repair_first = decode_frames(
            profiles[p], wire, std::set<size_t>{1u, 3u}, true, true);
        assert_packets_equal(packets, repair_first.received);

        const DecodeResult failed = decode_frames(
            profiles[p], wire, std::set<size_t>{0u, 1u, 2u}, false, false);
        assert(failed.received.packets.size() == packets.size() - 3u);
        assert(failed.received.unrecoverable.size() == 3u);
    }
}

void test_frame_validation() {
    std::vector<Bytes> packets;
    const Wire wire = encode_one_block(FEC_PROFILE_XOR_I_D4_L4, packets);
    fec_config_t config = make_config(FEC_PROFILE_XOR_I_D4_L4);
    Received received;
    fec_decoder_t *decoder = NULL;
    assert(fec_decoder_create(&config,
                              packet_callback,
                              event_callback,
                              &received,
                              &decoder) == FEC_OK);

    Bytes payload_corrupt = wire.frames[1];
    payload_corrupt[fec_frame_header_size()] ^= 0x55u;
    assert(fec_decoder_ingest(decoder,
                              &payload_corrupt[0],
                              payload_corrupt.size(),
                              1u) == FEC_ERR_CRC);

    Bytes header_corrupt = wire.frames[1];
    header_corrupt[6] ^= 1u;
    assert(fec_decoder_ingest(decoder,
                              &header_corrupt[0],
                              header_corrupt.size(),
                              1u) == FEC_ERR_CRC);

    Bytes invalid_type = wire.frames[1];
    invalid_type[4] = FEC_FRAME_REPAIR;
    refresh_header_crc(invalid_type);
    assert(fec_decoder_ingest(decoder,
                              &invalid_type[0],
                              invalid_type.size(),
                              1u) == FEC_ERR_FORMAT);

    Bytes invalid_length = wire.frames.back();
    write_u16(invalid_length, 26u, 1u);
    refresh_header_crc(invalid_length);
    assert(fec_decoder_ingest(decoder,
                              &invalid_length[0],
                              invalid_length.size(),
                              1u) == FEC_ERR_FORMAT);

    fec_decoder_stats_t stats;
    assert(fec_decoder_get_stats(decoder, &stats, 1) == FEC_OK);
    assert(stats.crc_errors == 2u);
    assert(stats.format_errors == 2u);
    assert(fec_decoder_get_stats(decoder, &stats, 0) == FEC_OK);
    assert(stats.crc_errors == 0u && stats.format_errors == 0u);
    fec_decoder_destroy(decoder);
}

void test_decoder_flush() {
    std::vector<Bytes> packets;
    const Wire wire = encode_one_block(FEC_PROFILE_XOR_I_D4_L4, packets);
    fec_config_t config = make_config(FEC_PROFILE_XOR_I_D4_L4);
    config.block_timeout_ms = 0u;
    Received received;
    fec_decoder_t *decoder = NULL;
    assert(fec_decoder_create(&config,
                              packet_callback,
                              event_callback,
                              &received,
                              &decoder) == FEC_OK);
    for (size_t i = 0u; i < wire.frames.size(); ++i) {
        if (i == 0u || i == 4u) {
            continue;
        }
        assert(fec_decoder_ingest(decoder,
                                  &wire.frames[i][0],
                                  wire.frames[i].size(),
                                  1u) == FEC_OK);
    }
    assert(received.unrecoverable.empty());
    assert(fec_decoder_flush(decoder) == FEC_OK);
    assert(received.unrecoverable.size() == 2u);
    fec_decoder_stats_t stats;
    assert(fec_decoder_get_stats(decoder, &stats, 0) == FEC_OK);
    assert(stats.unrecoverable_packets == 2u);
    assert(stats.expired_blocks == 1u);
    fec_decoder_destroy(decoder);
}

void test_profile_switch_and_auto_decoder() {
    fec_config_t encoder_config = make_config(FEC_PROFILE_XOR_I_D4_L4);
    Wire wire;
    fec_encoder_t *encoder = NULL;
    assert(fec_encoder_create(&encoder_config,
                              frame_callback,
                              &wire,
                              &encoder) == FEC_OK);

    const std::vector<Bytes> first = make_packets(16u);
    for (size_t i = 0u; i < first.size(); ++i) {
        if (i == 5u) {
            assert(fec_encoder_request_profile(encoder, FEC_PROFILE_RS_8_6) == FEC_OK);
            assert(fec_encoder_get_profile(encoder) == FEC_PROFILE_XOR_I_D4_L4);
        }
        const uint8_t *data = first[i].empty() ? NULL : &first[i][0];
        assert(fec_encoder_push(encoder, data, first[i].size()) == FEC_OK);
    }
    assert(fec_encoder_get_profile(encoder) == FEC_PROFILE_RS_8_6);
    const std::vector<Bytes> second = make_packets(6u);
    for (size_t i = 0u; i < second.size(); ++i) {
        const uint8_t *data = second[i].empty() ? NULL : &second[i][0];
        assert(fec_encoder_push(encoder, data, second[i].size()) == FEC_OK);
    }
    fec_encoder_destroy(encoder);
    assert(wire.frames.size() == 28u);
    for (size_t i = 0u; i < 20u; ++i) {
        assert(wire.frames[i][3] == FEC_PROFILE_XOR_I_D4_L4);
    }
    for (size_t i = 20u; i < wire.frames.size(); ++i) {
        assert(wire.frames[i][3] == FEC_PROFILE_RS_8_6);
        assert(read_u32(wire.frames[i], 14u) == 1u);
        assert(read_u32(wire.frames[i], 18u) == 16u);
    }

    fec_config_t decoder_config = make_config(FEC_PROFILE_AUTO);
    Received received;
    fec_decoder_t *decoder = NULL;
    assert(fec_decoder_create(&decoder_config,
                              packet_callback,
                              event_callback,
                              &received,
                              &decoder) == FEC_OK);
    for (size_t i = 0u; i < wire.frames.size(); ++i) {
        if (i == 2u || i == 21u || i == 22u) {
            continue;
        }
        assert(fec_decoder_ingest(decoder,
                                  &wire.frames[i][0],
                                  wire.frames[i].size(),
                                  1u) == FEC_OK);
    }
    assert(fec_decoder_expire(decoder, 200u) == FEC_OK);
    assert(received.packets.size() == 22u);
    for (size_t i = 0u; i < first.size(); ++i) {
        assert(received.packets[static_cast<uint32_t>(i)] == first[i]);
    }
    for (size_t i = 0u; i < second.size(); ++i) {
        assert(received.packets[static_cast<uint32_t>(16u + i)] == second[i]);
    }
    fec_decoder_destroy(decoder);
}

void test_flush_sequence() {
    const fec_config_t config = make_config(FEC_PROFILE_XOR_I_D4_L4);
    Wire wire;
    fec_encoder_t *encoder = NULL;
    assert(fec_encoder_create(&config, frame_callback, &wire, &encoder) == FEC_OK);
    const Bytes packet(5u, 0xA5u);
    for (int i = 0; i < 3; ++i) {
        assert(fec_encoder_push(encoder, &packet[0], packet.size()) == FEC_OK);
    }
    assert(fec_encoder_request_profile(encoder, FEC_PROFILE_RS_8_6) == FEC_OK);
    assert(fec_encoder_flush(encoder) == FEC_OK);
    assert(fec_encoder_push(encoder, &packet[0], packet.size()) == FEC_OK);
    fec_encoder_destroy(encoder);
    assert(read_u32(wire.frames.back(), 14u) == 1u);
    assert(read_u32(wire.frames.back(), 18u) == 3u);
    assert(wire.frames.back()[3] == FEC_PROFILE_RS_8_6);
}

void test_controller() {
    fec_controller_config_t config;
    std::memset(&config, 0, sizeof(config));
    config.initial_profile = FEC_PROFILE_XOR_I_D8_L4;
    config.min_hold_ms = 10u;
    config.window_ms = 10u;
    config.stable_windows_to_degrade = 3u;

    fec_controller_t *controller = NULL;
    assert(fec_controller_create(&config, &controller) == FEC_OK);
    fec_link_metrics_t metrics[2];
    std::memset(metrics, 0, sizeof(metrics));
    metrics[0].loss_rate_ppm = 1000u;
    metrics[1].loss_rate_ppm = 200000u;
    fec_profile_t profile = FEC_PROFILE_NONE;
    int changed = 0;
    assert(fec_controller_update(controller,
                                 metrics,
                                 2u,
                                 5u,
                                 &profile,
                                 &changed) == FEC_OK);
    assert(changed == 0);
    assert(fec_controller_update(controller,
                                 metrics,
                                 2u,
                                 10u,
                                 &profile,
                                 &changed) == FEC_OK);
    assert(changed == 1 && profile == FEC_PROFILE_XOR_I_D3_L4);

    std::memset(metrics, 0, sizeof(metrics));
    for (uint32_t now = 20u; now <= 30u; now += 10u) {
        assert(fec_controller_update(controller,
                                     metrics,
                                     2u,
                                     now,
                                     &profile,
                                     &changed) == FEC_OK);
        assert(changed == 0);
    }
    assert(fec_controller_update(controller,
                                 metrics,
                                 2u,
                                 40u,
                                 &profile,
                                 &changed) == FEC_OK);
    assert(changed == 1 && profile == FEC_PROFILE_XOR_I_D8_L4);

    metrics[0].max_burst = 8u;
    assert(fec_controller_update(controller,
                                 metrics,
                                 2u,
                                 50u,
                                 &profile,
                                 &changed) == FEC_OK);
    assert(changed == 1 && profile == FEC_PROFILE_XOR_I_D4_L8);
    metrics[0].max_burst = 0u;
    metrics[1].same_column_losses = 1u;
    assert(fec_controller_update(controller,
                                 metrics,
                                 2u,
                                 60u,
                                 &profile,
                                 &changed) == FEC_OK);
    assert(changed == 1 && profile == FEC_PROFILE_RS_8_6);
    fec_controller_destroy(controller);
}

} // namespace

int main() {
    test_profile_metadata();
    test_crc_standard_vector();
    test_all_profiles_single_erasure();
    test_xor_bursts_and_same_column_failure();
    test_rs_all_double_erasures();
    test_frame_validation();
    test_decoder_flush();
    test_profile_switch_and_auto_decoder();
    test_flush_sequence();
    test_controller();
    std::cout << "all fec tests passed" << std::endl;
    return 0;
}

#include "internal/reed_solomon_codec.h"

#include <cstring>
#include <utility>

namespace fec {
namespace internal {
namespace {

struct GfTables {
    uint8_t exponent[512];
    uint8_t logarithm[256];

    GfTables() : exponent(), logarithm() {
        uint16_t value = 1u;
        for (uint16_t i = 0u; i < 255u; ++i) {
            exponent[i] = static_cast<uint8_t>(value);
            logarithm[static_cast<uint8_t>(value)] = static_cast<uint8_t>(i);
            value <<= 1u;
            if ((value & 0x100u) != 0u) {
                /* RFC 5510 使用本原多项式 0x11d。 */
                value ^= 0x11Du;
            }
        }
        for (uint16_t i = 255u; i < 512u; ++i) {
            exponent[i] = exponent[i - 255u];
        }
    }
};

const GfTables kGfTables;

uint8_t multiply(uint8_t left, uint8_t right) {
    if (left == 0u || right == 0u) {
        return 0u;
    }
    return kGfTables.exponent[
        static_cast<uint16_t>(kGfTables.logarithm[left]) +
        kGfTables.logarithm[right]];
}

uint8_t power(uint8_t value, unsigned exponent) {
    if (exponent == 0u) {
        return 1u;
    }
    if (value == 0u) {
        return 0u;
    }
    const unsigned result =
        (static_cast<unsigned>(kGfTables.logarithm[value]) * exponent) % 255u;
    return kGfTables.exponent[result];
}

uint8_t inverse(uint8_t value) {
    return value == 0u ?
        0u : kGfTables.exponent[255u - kGfTables.logarithm[value]];
}

bool invert_matrix(const uint8_t input[kMaxEncodedSymbols][kMaxEncodedSymbols],
                   uint8_t output[kMaxEncodedSymbols][kMaxEncodedSymbols],
                   uint8_t size) {
    uint8_t augmented[kMaxEncodedSymbols][kMaxEncodedSymbols * 2u];
    std::memset(augmented, 0, sizeof(augmented));
    for (uint8_t row = 0u; row < size; ++row) {
        for (uint8_t column = 0u; column < size; ++column) {
            augmented[row][column] = input[row][column];
        }
        augmented[row][size + row] = 1u;
    }

    for (uint8_t pivot_column = 0u; pivot_column < size; ++pivot_column) {
        uint8_t pivot_row = pivot_column;
        while (pivot_row < size && augmented[pivot_row][pivot_column] == 0u) {
            ++pivot_row;
        }
        if (pivot_row == size) {
            return false;
        }
        if (pivot_row != pivot_column) {
            for (uint8_t column = 0u;
                 column < static_cast<uint8_t>(size * 2u);
                 ++column) {
                std::swap(augmented[pivot_row][column],
                          augmented[pivot_column][column]);
            }
        }

        const uint8_t pivot_inverse =
            inverse(augmented[pivot_column][pivot_column]);
        for (uint8_t column = 0u;
             column < static_cast<uint8_t>(size * 2u);
             ++column) {
            augmented[pivot_column][column] =
                multiply(augmented[pivot_column][column], pivot_inverse);
        }
        for (uint8_t row = 0u; row < size; ++row) {
            if (row == pivot_column || augmented[row][pivot_column] == 0u) {
                continue;
            }
            const uint8_t factor = augmented[row][pivot_column];
            for (uint8_t column = 0u;
                 column < static_cast<uint8_t>(size * 2u);
                 ++column) {
                augmented[row][column] ^=
                    multiply(factor, augmented[pivot_column][column]);
            }
        }
    }

    for (uint8_t row = 0u; row < size; ++row) {
        for (uint8_t column = 0u; column < size; ++column) {
            output[row][column] = augmented[row][size + column];
        }
    }
    return true;
}

} // namespace

ReedSolomonCodec::ReedSolomonCodec() : params_(), generator_() {
    std::memset(&params_, 0, sizeof(params_));
    std::memset(generator_, 0, sizeof(generator_));
}

bool ReedSolomonCodec::configure(const ProfileParams &params) {
    params_ = params;
    std::memset(generator_, 0, sizeof(generator_));
    if (params.algorithm != FEC_ALGORITHM_REED_SOLOMON) {
        return true;
    }

    uint8_t vandermonde[kMaxEncodedSymbols][kMaxEncodedSymbols];
    uint8_t first_square[kMaxEncodedSymbols][kMaxEncodedSymbols];
    uint8_t first_inverse[kMaxEncodedSymbols][kMaxEncodedSymbols];
    std::memset(vandermonde, 0, sizeof(vandermonde));
    std::memset(first_square, 0, sizeof(first_square));
    std::memset(first_inverse, 0, sizeof(first_inverse));

    for (uint8_t source = 0u; source < params.source_count; ++source) {
        for (uint8_t encoded = 0u; encoded < params.total_count; ++encoded) {
            vandermonde[source][encoded] =
                power(2u, static_cast<unsigned>(source) * encoded);
            if (encoded < params.source_count) {
                first_square[source][encoded] = vandermonde[source][encoded];
            }
        }
    }
    if (!invert_matrix(first_square, first_inverse, params.source_count)) {
        return false;
    }
    for (uint8_t encoded = 0u; encoded < params.total_count; ++encoded) {
        for (uint8_t source = 0u; source < params.source_count; ++source) {
            uint8_t coefficient = 0u;
            for (uint8_t inner = 0u; inner < params.source_count; ++inner) {
                coefficient ^= multiply(first_inverse[source][inner],
                                        vandermonde[inner][encoded]);
            }
            generator_[encoded][source] = coefficient;
        }
    }
    return true;
}

void ReedSolomonCodec::encode_repair(uint8_t encoded_index,
                                     const uint8_t *source_symbols,
                                     std::size_t symbol_size,
                                     uint8_t *repair) const {
    std::memset(repair, 0, symbol_size);
    for (uint8_t source = 0u; source < params_.source_count; ++source) {
        const uint8_t coefficient = generator_[encoded_index][source];
        const uint8_t *symbol =
            source_symbols + static_cast<std::size_t>(source) * symbol_size;
        for (std::size_t offset = 0u; offset < symbol_size; ++offset) {
            repair[offset] ^= multiply(coefficient, symbol[offset]);
        }
    }
}

bool ReedSolomonCodec::prepare_recovery(
    uint32_t available_mask,
    uint8_t selected[kMaxEncodedSymbols],
    uint8_t decode_inverse[kMaxEncodedSymbols][kMaxEncodedSymbols]) const {
    uint8_t selected_count = 0u;
    for (uint8_t index = 0u;
         index < params_.total_count && selected_count < params_.source_count;
         ++index) {
        if ((available_mask & (static_cast<uint32_t>(1u) << index)) != 0u) {
            selected[selected_count++] = index;
        }
    }
    if (selected_count < params_.source_count) {
        return false;
    }

    uint8_t equations[kMaxEncodedSymbols][kMaxEncodedSymbols];
    std::memset(equations, 0, sizeof(equations));
    std::memset(decode_inverse, 0,
                kMaxEncodedSymbols * kMaxEncodedSymbols * sizeof(uint8_t));
    for (uint8_t row = 0u; row < params_.source_count; ++row) {
        for (uint8_t column = 0u; column < params_.source_count; ++column) {
            equations[row][column] = generator_[selected[row]][column];
        }
    }
    return invert_matrix(equations, decode_inverse, params_.source_count);
}

void ReedSolomonCodec::recover_source(
    uint8_t source_index,
    const uint8_t selected[kMaxEncodedSymbols],
    const uint8_t decode_inverse[kMaxEncodedSymbols][kMaxEncodedSymbols],
    const uint8_t *encoded_symbols,
    std::size_t symbol_size,
    uint8_t *output) const {
    std::memset(output, 0, symbol_size);
    for (uint8_t equation = 0u; equation < params_.source_count; ++equation) {
        const uint8_t coefficient = decode_inverse[source_index][equation];
        const uint8_t *input = encoded_symbols +
            static_cast<std::size_t>(selected[equation]) * symbol_size;
        for (std::size_t offset = 0u; offset < symbol_size; ++offset) {
            output[offset] ^= multiply(coefficient, input[offset]);
        }
    }
}

} // namespace internal
} // namespace fec

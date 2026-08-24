#ifndef FEC_INTERNAL_XOR_CODEC_H
#define FEC_INTERNAL_XOR_CODEC_H

#include <cstddef>
#include <cstdint>

namespace fec {
namespace internal {

/* XOR 算法本身无状态，调用方负责按交织列选择累加器。 */
class XorCodec {
public:
    static void accumulate(uint8_t *destination,
                           const uint8_t *source,
                           std::size_t size);

    static void recover(const uint8_t *repair,
                        const uint8_t *received_accumulator,
                        uint8_t *output,
                        std::size_t size);
};

} // namespace internal
} // namespace fec

#endif

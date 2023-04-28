%{
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/Common.h"

using namespace bcos;
typedef unsigned long size_t;

inline bcos::bytesConstRef newBytesConstRef(std::vector<uint8_t> const& input) {
    return {input.data(), input.size()};
}
inline bcos::bytes h256ToBytes(h256 const& input) {
    return {input.data(), input.data() + input.size()};
}
%}

%include <stdint.i>
%include <std_vector.i>

typedef unsigned long size_t;
%template(bytes) std::vector<uint8_t>;

%include "../bcos-utilities/bcos-utilities/RefDataContainer.h"
inline bcos::bytesConstRef newBytesConstRef(std::vector<uint8_t> const& input);
inline bcos::bytes h256ToBytes(h256 const& input);

using bcos::byte = uint8_t;
using bcos::bytes = std::vector<bcos::byte>;
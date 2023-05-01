%{
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/Common.h"
#include <string_view>

using namespace bcos;
typedef unsigned long size_t;

inline bcos::bytesConstRef newBytesConstRef(std::vector<uint8_t> const& input) {
    return {input.data(), input.size()};
}
inline bcos::bytes h256ToBytes(h256 const& input) {
    return {input.data(), input.data() + input.size()};
}
inline std::string stringViewToString(std::string_view view) {
    return std::string{view};
}
%}

%include <stdint.i>
%include <std_vector.i>
%include <std_string.i>

typedef unsigned long size_t;
%template(bytes) std::vector<uint8_t>;

using bcos::byte = uint8_t;
using bcos::bytes = std::vector<bcos::byte>;

inline bcos::bytesConstRef newBytesConstRef(std::vector<uint8_t> const& input);
inline bcos::bytes h256ToBytes(h256 const& input);
inline std::string stringViewToString(std::string_view view);
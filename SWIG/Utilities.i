%{
#include "bcos-utilities/FixedBytes.h"

using namespace bcos;
inline bcos::bytes newBytes(char* str, size_t length) {
    return bcos::bytes((bcos::byte*)str, (bcos::byte*)str + length);
}
%}

%include <stdint.i>
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
#include "../bcos-utilities/bcos-utilities/FixedBytes.h"

inline bcos::bytes newBytes(char* str, size_t length);
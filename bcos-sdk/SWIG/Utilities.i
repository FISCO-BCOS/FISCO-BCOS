%{
#include "SWIG/Helper.h"
%}

%include <stdint.i>
%include <std_vector.i>
%include <std_string.i>
%include "Helper.h"

using bcos::byte = uint8_t;
using bcos::bytes = std::vector<bcos::byte>;

%template(toBytesConstRef) bcos::sdk::swig::toBytesConstRef<std::string_view>;
%template(toBytesConstRef) bcos::sdk::swig::toBytesConstRef<std::string>;
%template(toBytesConstRef) bcos::sdk::swig::toBytesConstRef<bcos::bytes>;

%template(toBytes) bcos::sdk::swig::toBytes<std::string_view>;
%template(toBytes) bcos::sdk::swig::toBytes<std::string>;
%template(toBytes) bcos::sdk::swig::toBytes<bcos::bytesConstRef>;

%template(toString) bcos::sdk::swig::toString<std::string_view>;
%template(toString) bcos::sdk::swig::toString<bcos::bytesConstRef>;
%template(toString) bcos::sdk::swig::toString<bcos::bytes>;

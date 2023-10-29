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
%template(toString) bcos::sdk::swig::toString<bcos::u256>;

%template(toHex) bcos::sdk::swig::toHex<std::string_view>;
%template(toHex) bcos::sdk::swig::toHex<bcos::bytesConstRef>;
%template(toHex) bcos::sdk::swig::toHex<bcos::bytes>;
%template(toHex) bcos::sdk::swig::toHex<bcos::h256>;

%template(fromHex) bcos::sdk::swig::fromHex<std::string_view>;
%template(fromHex) bcos::sdk::swig::fromHex<std::string>;
%template(fromHex) bcos::sdk::swig::fromHex<bcos::bytesConstRef>;
%template(fromHex) bcos::sdk::swig::fromHex<bcos::bytes>;

%apply (char *STRING, size_t LENGTH) { (const char data[], size_t len) }
%template(fillBytes) bcos::sdk::swig::fillBytes<std::string_view>;
%template(fillBytes) bcos::sdk::swig::fillBytes<bcos::bytesConstRef>;
%template(fillBytes) bcos::sdk::swig::fillBytes<bcos::bytes>;
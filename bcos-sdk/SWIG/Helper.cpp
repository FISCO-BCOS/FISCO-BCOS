#include "Helper.h"

bcos::bytesConstRef bcos::sdk::swig::toBytesConstRef(char* STRING, size_t LENGTH)
{
    return {(const bcos::byte*)STRING, LENGTH};
}

bcos::bytes bcos::sdk::swig::toBytes(char* STRING, size_t LENGTH)
{
    return {(const bcos::byte*)STRING, (const bcos::byte*)STRING + LENGTH};
}

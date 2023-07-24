#include "Helper.h"

bcos::bytesConstRef bcos::sdk::swig::toBytesConstRef(char* STRING, size_t LENGTH)
{
    return {(const bcos::byte*)STRING, LENGTH};
}

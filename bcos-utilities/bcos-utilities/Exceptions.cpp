#include "Exceptions.h"

const char* bcos::Exception::what() const _GLIBCXX_TXN_SAFE_DYN _GLIBCXX_NOTHROW
{
    if (const std::string* comment = boost::get_error_info<errinfo_comment>(*this))
    {
        return comment->c_str();
    }
    return "";
}

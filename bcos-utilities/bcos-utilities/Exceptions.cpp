#include "Exceptions.h"

const char* bcos::Exception::what() const noexcept
{
    if (const std::string* comment = boost::get_error_info<errinfo_comment>(*this))
    {
        return comment->c_str();
    }
    return "";
}

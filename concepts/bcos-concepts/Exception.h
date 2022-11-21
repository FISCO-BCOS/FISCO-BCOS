#pragma once

#include <boost/exception/error_info.hpp>
#include <boost/exception/exception.hpp>
#include <exception>

namespace bcos::error
{
struct Exception : public virtual std::exception, public virtual boost::exception
{
};

using ErrorCode = boost::error_info<struct ErrorCodeTag, int>;
using ErrorMessage = boost::error_info<struct ErrorMessageTag, std::string>;
}  // namespace bcos::error
#pragma once

#include <concepts>
#include <exception>
#include <functional>

namespace bcos::tool
{
class ExceptionHolder
{
public:
    auto operator()(std::invocable auto&& func) noexcept
    {
        try
        {
            return func();
        }
        catch (...)
        {
            m_exception = std::current_exception();
        }
    }

    void rethrow()
    {
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }
    }


private:
    std::exception_ptr m_exception;
};
}  // namespace bcos::tool
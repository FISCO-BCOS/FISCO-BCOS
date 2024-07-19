#pragma once

#include <version>

#if __cpp_lib_generator >= 202207L
#include <generator>
namespace bcos::task
{
template <typename Ref, typename Val = void, typename Alloc = void>
using Generator = std::generator<Ref, Val, Alloc>;
}
#else
#include <coroutine>
#include <exception>
#include <iterator>
#include <type_traits>
#include <utility>
namespace bcos::task
{
// Fork from https://open-std.org/JTC1/SC22/WG21/docs/papers/2020/p2168r0.pdf
// Can be replace with std::generator

template <typename Ref, typename Value = std::remove_cvref_t<Ref>, typename Alloc = void>
class Generator
{
public:
    class promise_type
    {
    public:
        promise_type() : m_promise(this) {}
        Generator get_return_object() noexcept
        {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        void unhandled_exception()
        {
            if (m_exception == nullptr)
            {
                throw;
            }
            *m_exception = std::current_exception();
        }

        void return_void() noexcept {}

        std::suspend_always initial_suspend() noexcept { return {}; }

        // Transfers control back to the parent of a nested coroutine
        struct final_awaiter
        {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
            {
                auto& promise = h.promise();
                auto parent = h.promise().m_parent;
                if (parent)
                {
                    promise.m_promise = parent;
                    return std::coroutine_handle<promise_type>::from_promise(*parent);
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        final_awaiter final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(Ref&& x) noexcept
        {
            m_promise->m_value = std::addressof(x);
            return {};
        }
        std::suspend_always yield_value(Ref& x) noexcept
        {
            m_promise->m_value = std::addressof(x);
            return {};
        }

        struct yield_sequence_awaiter
        {
            Generator gen_;
            std::exception_ptr exception_;

            explicit yield_sequence_awaiter(Generator&& g) noexcept
              // Taking ownership of the generator ensures frame are destroyed
              // in the reverse order of their creation
              : gen_(std::move(g))
            {}

            bool await_ready() noexcept { return !gen_.coro_; }

            // set the parent, root and exceptions pointer and
            // resume the nested coroutine
            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> handle) noexcept
            {
                auto& current = handle.promise();
                auto& nested = gen_.coro_.promise();
                auto& root = current.root_;

                nested.root_ = root;
                root->leaf_ = &nested;
                nested.parent_ = &current;

                nested.exception_ = &exception_;

                // Immediately resume the nested coroutine (nested generator)
                return gen_.coro_;
            }

            void await_resume()
            {
                if (exception_)
                {
                    std::rethrow_exception(std::move(exception_));
                }
            }
        };

        yield_sequence_awaiter yield_value(Generator&& g) noexcept
        {
            return yield_sequence_awaiter{std::move(g)};
        }

        void resume() { std::coroutine_handle<promise_type>::from_promise(*m_promise).resume(); }

        // Disable use of co_await within this coroutine.
        void await_transform() = delete;

    private:
        friend Generator;

        promise_type* m_promise;
        promise_type* m_parent = nullptr;
        std::exception_ptr* m_exception = nullptr;
        std::add_pointer_t<Ref> m_value;
    };

    Generator() noexcept = default;

    Generator(const Generator&) = delete;
    Generator& operator=(Generator&&) = delete;
    Generator(Generator&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

    ~Generator() noexcept
    {
        if (coro_)
        {
            coro_.destroy();
        }
    }

    Generator& operator=(Generator g) noexcept
    {
        swap(g);
        return *this;
    }

    void swap(Generator& other) noexcept { std::swap(coro_, other.coro_); }

    struct sentinel
    {
    };

    class Iterator
    {
        using coroutine_handle = std::coroutine_handle<promise_type>;

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Value;
        using reference = Ref;
        using pointer = std::add_pointer_t<Ref>;

        Iterator& operator=(const Iterator&) = delete;
        Iterator(const Iterator&) = delete;
        Iterator(Iterator&& o) noexcept { std::swap(coro_, o.coro_); }

        Iterator& operator=(Iterator&& o) noexcept
        {
            std::swap(coro_, o.coro_);
            return *this;
        }

        ~Iterator() = default;

        friend bool operator==(const Iterator& it, sentinel) noexcept
        {
            return !it.coro_ || it.coro_.done();
        }

        Iterator& operator++()
        {
            coro_.promise().resume();
            return *this;
        }
        void operator++(int) { (void)operator++(); }

        reference operator*() const noexcept
        {
            return static_cast<reference>(*coro_.promise().m_value);
        }

        pointer operator->() const noexcept
            requires std::is_reference_v<reference>
        {
            return std::addressof(operator*());
        }

    private:
        friend Generator;

        explicit Iterator(coroutine_handle coro) noexcept : coro_(coro) {}

        coroutine_handle coro_;
    };

    Iterator begin()
    {
        if (coro_)
        {
            coro_.resume();
        }
        return Iterator{coro_};
    }

    sentinel end() noexcept { return {}; }

private:
    explicit Generator(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) {}

    std::coroutine_handle<promise_type> coro_;
};
}  // namespace bcos::task
#endif
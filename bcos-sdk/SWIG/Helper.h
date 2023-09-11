#pragma once

#include "bcos-cpp-sdk/tarsRPC/Handle.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <oneapi/tbb/concurrent_queue.h>
#include <memory>
#include <string_view>
namespace bcos::sdk::swig
{

template <class Buffer>
bcos::bytesConstRef toBytesConstRef(Buffer const& input)
{
    return {(const bcos::byte*)input.data(), input.size()};
}

template <class Buffer>
bcos::bytes toBytes(Buffer const& input)
{
    return {(const bcos::byte*)input.data(), (const bcos::byte*)input.data() + input.size()};
}
bcos::bytes toBytes(char* STRING, size_t LENGTH);

template <class Buffer>
h256 toH256(Buffer const& input)
{
    return {input};
}

template <class Input>
concept HasMemberConvertTo = requires(Input&& input) {
                                 {
                                     input.template convert_to<std::string>()
                                     } -> std::same_as<std::string>;
                             };
template <class Input>
std::string toString(Input const& input)
{
    if constexpr (HasMemberConvertTo<Input>)
    {
        return input.template convert_to<std::string>();
    }
    else
    {
        return {(const char*)input.data(), input.size()};
    }
}

template <class Buffer>
std::string_view toStringView(Buffer input)
{
    return {(const char*)input.data(), input.size()};
}

template <class Input>
concept HasMemberHex = requires(Input&& input) {
                           {
                               input.hex()
                               } -> std::same_as<std::string>;
                       };
template <class Input>
std::string toHex(Input const& input)
{
    if constexpr (HasMemberHex<Input>)
    {
        return input.hex();
    }
    else
    {
        return bcos::toHex(input);
    }
}

template <class Buffer>
void fillBytes(Buffer const& input, char* STRING, size_t LENGTH)
{
    std::copy(input.data(), input.data() + LENGTH, STRING);
}

class ConcurrentQueue
{
private:
    oneapi::tbb::concurrent_bounded_queue<int> m_queue;

public:
    void push(int seq) { m_queue.push(seq); }
    int pop()
    {
        int seq{};
        m_queue.pop(seq);
        return seq;
    }
};

class ConcurrentQueueCallback : public bcos::sdk::Callback
{
private:
    ConcurrentQueue& m_queue;
    int m_seq;

public:
    ConcurrentQueueCallback(ConcurrentQueue& queue, int seq) : m_queue(queue), m_seq(seq) {}
    void onMessage() override { m_queue.push(m_seq); }
};

}  // namespace bcos::sdk::swig
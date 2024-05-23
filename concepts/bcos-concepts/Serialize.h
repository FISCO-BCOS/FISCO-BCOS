#pragma once

namespace bcos::concepts::serialize
{
namespace detail
{

template <class Object, class Buffer>
concept HasMemberFuncEncode =
    requires(Object const& object, Buffer& output) { object.encode(output); };
template <class Object, class Buffer>
concept HasMemberFuncDecode =
    requires(Object object, const Buffer& input) { object.decode(input); };
template <class Object, class Buffer>
concept HasADLEncode =
    requires(Object const& object, Buffer& output) { impl_encode(object, output); };
template <class Object, class Buffer>
concept HasADLDecode = requires(Object object, const Buffer& input) { impl_decode(input, object); };
template <class Object>
concept HasADLPreEncode = requires(Object object) { impl_pre_encode(object); };
template <class Object>
concept HasADLAfterDecode = requires(Object object) { impl_after_decode(object); };

struct encode
{
    template <class Object, class Buffer>
    void operator()(const Object& object, Buffer& out) const
        requires HasMemberFuncEncode<Object, Buffer>
    {
        object.encode(out);
    }

    template <class Object, class Buffer>
    void operator()(const Object& object, Buffer& out) const
        requires HasADLEncode<Object, Buffer>
    {
        impl_encode(object, out);
    }
};

struct decode
{
    template <class Buffer, class Object>
    void operator()(Buffer const& input, Object& object) const
        requires HasMemberFuncDecode<Object, Buffer>
    {
        object.decode(input);
    }

    template <class Buffer, class Object>
    void operator()(Buffer const& input, Object& object) const
        requires HasADLDecode<Object, Buffer>
    {
        impl_decode(input, object);
    }
};

struct PreEncode
{
    template <class Object>
    void operator()(Object& object) const
        requires HasADLPreEncode<Object>
    {
        impl_pre_encode(object);
    }
};

struct AfterDecode
{
    template <class Object>
    void operator()(Object& object) const
        requires HasADLAfterDecode<Object>
    {
        impl_after_decode(object);
    }
};
}  // namespace detail

constexpr inline detail::encode encode{};
constexpr inline detail::decode decode{};
constexpr inline detail::PreEncode pre_encode{};
constexpr inline detail::AfterDecode after_decode{};

template <class Object>
concept Serializable = true;

}  // namespace bcos::concepts::serialize
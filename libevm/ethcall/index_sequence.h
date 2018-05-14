/**
 * @file: index_sequence.h
 * @author: fisco-dev
 * 
 * @date: 2017
 * This is c++14 standard function, c++11 should add here
 */

#include <tuple>
#include <type_traits>
#include <array>

//T an ingeter type to use for the elements of the sequence
//...Ints a non-type parameter pack representing the sequence
template<typename T, T... Ints>
class integer_sequence
{
public:
    using value_type = T;
    static_assert(std::is_integral<value_type>::value, "Please use integral type when using integer_sequence.");

    static constexpr std::size_t size() noexcept
    {
        return sizeof...(Ints);
    }
};

namespace internal
{ 
    template<typename T, T Begin, T End, bool>
    struct make_seq
    {
        using value_type = T;

        template<typename, typename>
        struct seq_combine;

        template<value_type... Ints0, value_type... Ints1>
        struct seq_combine<integer_sequence<value_type, Ints0...>, integer_sequence<value_type, Ints1...>>
        {
            using result_type = integer_sequence<value_type, Ints0..., Ints1...>;
        };

        using result_type = typename seq_combine<typename make_seq<value_type, Begin, Begin + (End - Begin) / 2, 
                                                                   (End - Begin) / 2 == 1>::result_type, 
                                                 typename make_seq<value_type, Begin + (End - Begin) / 2, End,
                                                                   (End - Begin + 1) / 2 == 1>::result_type>::result_type;
    };

    template<typename T, T Begin, T End>
    struct make_seq<T, Begin, End, true>
    {
        using result_type = integer_sequence<T, Begin>;
    };
}

template<std::size_t...Ints>
using index_sequence = integer_sequence<std::size_t, Ints...>;

template<typename T, T N>
using make_integer_sequence = typename internal::make_seq<T, 0, N, (N - 0) == 1>::result_type;

template<std::size_t N>
using make_index_sequence = make_integer_sequence<std::size_t, N>;

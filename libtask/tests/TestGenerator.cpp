#include "bcos-task/Generator.h"
#include <boost/test/unit_test.hpp>
#include <array>
#include <concepts>
#include <memory>
#include <ranges>
#include <stdexcept>

using namespace bcos::task;

BOOST_AUTO_TEST_SUITE(GeneratorTest)

// GCC 14+ -Wmismatched-new-delete is triggered inside the coroutine bodies below at -Os +
// -Werror. Depending on the selected Generator.h branch, the promise allocator comes from
// libstdc++ or this project. Scope the suppression to these constructs rather than downgrading
// the warning for the whole project. Same idiom as bcos-utilities/BucketMap.h.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 14
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
Generator<int> genInt()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

Generator<int> genNestedInt()
{
    co_yield 0;
    co_yield elements_of(genInt());
    co_yield 4;
}

Generator<int> genRange()
{
    co_yield elements_of(std::views::iota(0, 3));
}

Generator<int, void, std::allocator<std::byte>> genWithAllocator(
    std::allocator_arg_t, std::allocator<std::byte>, int value)
{
    co_yield value;
}

Generator<int> genThrowingNested()
{
    co_yield 1;
    throw std::runtime_error("nested generator error");
}

Generator<int> genNestedException()
{
    co_yield elements_of(genThrowingNested());
}

Generator<int&> genReferences(std::array<int, 2>& values)
{
    for (auto& value : values)
    {
        co_yield value;
    }
}

Generator<int> genCopy()
{
    int value = 7;
    co_yield value;
}

Generator<int, int> genValue()
{
    int value = 8;
    co_yield value;
}
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 14
#pragma GCC diagnostic pop
#endif

BOOST_AUTO_TEST_CASE(generator)
{
    static_assert(std::same_as<std::ranges::range_reference_t<Generator<int>>, int&&>);
    static_assert(std::same_as<std::ranges::range_value_t<Generator<int>>, int>);
    static_assert(std::ranges::input_range<Generator<int>>);
    static_assert(std::ranges::view<Generator<int>>);
    static_assert(std::same_as<Generator<int, int>::yielded, const int&>);

    int j = 0;
    for (auto i : genInt())
    {
        BOOST_CHECK_EQUAL(i, ++j);
    }

    j = 0;
    for (auto i : genNestedInt())
    {
        BOOST_CHECK_EQUAL(i, j++);
    }
    BOOST_CHECK_EQUAL(j, 5);
}

BOOST_AUTO_TEST_CASE(genericRange)
{
    int expected = 0;
    for (auto value : genRange())
    {
        BOOST_CHECK_EQUAL(value, expected++);
    }
    BOOST_CHECK_EQUAL(expected, 3);
}

BOOST_AUTO_TEST_CASE(allocator)
{
    int count = 0;
    for (auto value : genWithAllocator(std::allocator_arg, std::allocator<std::byte>{}, 9))
    {
        BOOST_CHECK_EQUAL(value, 9);
        ++count;
    }
    BOOST_CHECK_EQUAL(count, 1);
}

BOOST_AUTO_TEST_CASE(nestedException)
{
    auto generator = genNestedException();
    auto iterator = generator.begin();
    BOOST_CHECK_EQUAL(*iterator, 1);
    BOOST_CHECK_THROW(++iterator, std::runtime_error);
}

BOOST_AUTO_TEST_CASE(referenceAndCopy)
{
    std::array values{4, 5};
    for (auto& value : genReferences(values))
    {
        value *= 2;
    }
    BOOST_CHECK_EQUAL(values[0], 8);
    BOOST_CHECK_EQUAL(values[1], 10);

    for (auto value : genCopy())
    {
        BOOST_CHECK_EQUAL(value, 7);
    }
    for (auto value : genValue())
    {
        BOOST_CHECK_EQUAL(value, 8);
    }
}

BOOST_AUTO_TEST_SUITE_END()

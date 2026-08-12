/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-utilities/HoldablePointer.h"
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{
namespace
{
// Test payload: records destruction so the test can observe whether the
// HoldablePointer actually released the owned object.
struct TestData
{
    static inline int s_alive = 0;

    int value;
    explicit TestData(int initValue) : value(initValue) { ++s_alive; }
    TestData(TestData&& other) noexcept : value(other.value) { ++s_alive; }
    TestData& operator=(TestData&& other) noexcept
    {
        value = other.value;
        return *this;
    }
    TestData(const TestData&) = delete;
    TestData& operator=(const TestData&) = delete;
    ~TestData() { --s_alive; }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(HoldablePointerTest)

// Borrowed pointer: points to an externally owned object, never releases it.
BOOST_AUTO_TEST_CASE(borrowedPointer)
{
    TestData data(42);
    bcos::HoldablePointer<TestData> ptr(bcos::isOwner<false>{}, &data);

    BOOST_CHECK_EQUAL(ptr->value, 42);
    BOOST_CHECK_EQUAL((*ptr).value, 42);

    // Reset with an empty pointer; the borrowed object must survive.
    ptr = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(data.value, 42);
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);
}

// Owned pointer: the HoldablePointer owns and releases the heap object.
BOOST_AUTO_TEST_CASE(ownedPointer)
{
    {
        bcos::HoldablePointer<TestData> ptr(bcos::isOwner<true>{}, new TestData(7));
        BOOST_CHECK_EQUAL(TestData::s_alive, 1);
        BOOST_CHECK_EQUAL(ptr->value, 7);
        BOOST_CHECK_EQUAL((*ptr).value, 7);
    }
    // Destruction must delete the owned object.
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Move construction transfers ownership: the source becomes empty and must not
// release the data, the destination releases it on destruction.
BOOST_AUTO_TEST_CASE(moveConstruct)
{
    bcos::HoldablePointer<TestData> src(bcos::isOwner<true>{}, new TestData(3));
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);

    bcos::HoldablePointer<TestData> dst(std::move(src));
    BOOST_CHECK_EQUAL(dst->value, 3);

    // src is empty now; destroying it must not release the object.
    src = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);

    dst = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Move assignment transfers ownership and leaves the source empty.
BOOST_AUTO_TEST_CASE(moveAssign)
{
    bcos::HoldablePointer<TestData> src(bcos::isOwner<true>{}, new TestData(5));
    bcos::HoldablePointer<TestData> dst;

    dst = std::move(src);
    BOOST_CHECK_EQUAL(dst->value, 5);

    src = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);

    dst = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Move-assigning onto an already-owned pointer must release the previous
// object (regression for a leak in the move-assign operator).
BOOST_AUTO_TEST_CASE(moveAssignOverOwned)
{
    bcos::HoldablePointer<TestData> dst(bcos::isOwner<true>{}, new TestData(1));
    bcos::HoldablePointer<TestData> src(bcos::isOwner<true>{}, new TestData(2));
    BOOST_CHECK_EQUAL(TestData::s_alive, 2);

    dst = std::move(src);
    // The old dst object must be released, only the moved one survives.
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);
    BOOST_CHECK_EQUAL(dst->value, 2);

    dst = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Self move-assignment must be safe and must not release the object.
// (The alias avoids clang's "moving to itself" diagnostic while still
// exercising the self-move path.)
BOOST_AUTO_TEST_CASE(selfMoveAssign)
{
    bcos::HoldablePointer<TestData> ptr(bcos::isOwner<true>{}, new TestData(9));
    auto& ptrRef = ptr;
    ptr = std::move(ptrRef);
    BOOST_CHECK_EQUAL(ptr->value, 9);
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);

    ptr = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Default-constructed pointer is empty and can be move-assigned into.
BOOST_AUTO_TEST_CASE(defaultConstructed)
{
    bcos::HoldablePointer<TestData> ptr;

    ptr = bcos::HoldablePointer<TestData>(bcos::isOwner<true>{}, new TestData(1));
    BOOST_CHECK_EQUAL(ptr->value, 1);
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);

    ptr = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// Borrowed pointer is accessible through a const HoldablePointer.
BOOST_AUTO_TEST_CASE(constAccess)
{
    TestData data(11);
    const bcos::HoldablePointer<TestData> ptr(bcos::isOwner<false>{}, &data);

    BOOST_CHECK_EQUAL(ptr->value, 11);
    BOOST_CHECK_EQUAL((*ptr).value, 11);
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);
}

// A borrowed pointer taken from a container element stays valid as long as the
// container lives (the core use case for the BlockImpl transaction views).
BOOST_AUTO_TEST_CASE(borrowedFromContainer)
{
    std::vector<TestData> container;
    container.emplace_back(1);
    container.emplace_back(2);
    container.emplace_back(3);

    std::vector<bcos::HoldablePointer<TestData>> views;
    views.reserve(container.size());
    for (auto& elem : container)
    {
        views.emplace_back(bcos::isOwner<false>{}, &elem);
    }

    int sum = 0;
    for (auto& view : views)
    {
        sum += view->value;
    }
    BOOST_CHECK_EQUAL(sum, 6);
    // No element must be released while the container is alive.
    BOOST_CHECK_EQUAL(TestData::s_alive, 3);

    container.clear();
    BOOST_CHECK_EQUAL(TestData::s_alive, 0);
}

// bool conversion reflects whether data is held, regardless of ownership mode.
BOOST_AUTO_TEST_CASE(boolConversion)
{
    TestData data(1);
    bcos::HoldablePointer<TestData> borrowed(bcos::isOwner<false>{}, &data);
    bcos::HoldablePointer<TestData> owned(bcos::isOwner<true>{}, new TestData(2));
    bcos::HoldablePointer<TestData> empty;

    BOOST_CHECK(borrowed);
    BOOST_CHECK(owned);
    BOOST_CHECK(!empty);

    // An empty pointer compares equal to nullptr.
    BOOST_CHECK(empty == nullptr);
    BOOST_CHECK(nullptr == empty);
    BOOST_CHECK(borrowed != nullptr);
    BOOST_CHECK(nullptr != borrowed);
}

// get() returns the untagged raw pointer for both const and non-const access.
BOOST_AUTO_TEST_CASE(getRawPointer)
{
    TestData data(1);
    bcos::HoldablePointer<TestData> ptr(bcos::isOwner<false>{}, &data);

    BOOST_CHECK_EQUAL(ptr.get(), &data);

    const bcos::HoldablePointer<TestData>& constRef = ptr;
    BOOST_CHECK_EQUAL(constRef.get(), &data);

    // get() on a moved-from pointer is null.
    bcos::HoldablePointer<TestData> moved(std::move(ptr));
    BOOST_CHECK_EQUAL(ptr.get(), nullptr);
    BOOST_CHECK_EQUAL(moved.get(), &data);
}

// operator== compares the pointed-to object, not the ownership tag.
BOOST_AUTO_TEST_CASE(equalityCompare)
{
    TestData data(1);
    bcos::HoldablePointer<TestData> borrowed(bcos::isOwner<false>{}, &data);
    bcos::HoldablePointer<TestData> otherBorrowed(bcos::isOwner<false>{}, &data);
    bcos::HoldablePointer<TestData> empty;

    // Same address borrowed by two pointers compares equal.
    BOOST_CHECK(borrowed == otherBorrowed);
    BOOST_CHECK(!(borrowed != otherBorrowed));

    // Different addresses compare unequal.
    BOOST_CHECK(borrowed != empty);
    BOOST_CHECK(empty == nullptr);
    BOOST_CHECK(borrowed != nullptr);

    // Mixed ownership: a borrowed pointer and an owned pointer to the same heap
    // object compare equal, because == only looks at the pointed-to address.
    auto* heapData = new TestData(1);
    bcos::HoldablePointer<TestData> owned(bcos::isOwner<true>{}, heapData);
    bcos::HoldablePointer<TestData> borrowedSame(bcos::isOwner<false>{}, heapData);
    BOOST_CHECK(owned == borrowedSame);
    BOOST_CHECK(borrowed != owned);

    // Release the owned heap object before leaving the scope.
    owned = bcos::HoldablePointer<TestData>();
    BOOST_CHECK_EQUAL(TestData::s_alive, 1);  // only the stack object survives
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

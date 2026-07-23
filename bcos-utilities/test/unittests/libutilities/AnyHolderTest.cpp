/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "bcos-utilities/AnyHolder.h"
#include <boost/test/unit_test.hpp>
#include <memory>

namespace bcos::test
{
namespace
{
struct Base
{
    virtual ~Base() = default;
    virtual int value() const = 0;
};
struct DerivedA : Base
{
    int v;
    explicit DerivedA(int x) : v(x) {}
    int value() const override { return v; }
};
struct DerivedB : Base
{
    int value() const override { return 100; }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(AnyHolderTest)

// In-place construct, deref operators, move-construct, same-type and
// different-type move-assign, and the toUnique/toShared escape hatches.
BOOST_AUTO_TEST_CASE(inPlaceMoveAndConvert)
{
    AnyHolder<Base, 64> holder(bcos::InPlace<DerivedA>{}, 42);
    BOOST_CHECK_EQUAL(holder->value(), 42);
    BOOST_CHECK_EQUAL((*holder).value(), 42);

    // Move-construct carries the concrete payload.
    AnyHolder<Base, 64> moved(std::move(holder));
    BOOST_CHECK_EQUAL(moved->value(), 42);

    // Same-type move-assign takes the moveAssign path.
    AnyHolder<Base, 64> sameType(bcos::InPlace<DerivedA>{}, 7);
    moved = std::move(sameType);
    BOOST_CHECK_EQUAL(moved->value(), 7);

    // Different-type move-assign destroys and move-constructs the new type.
    AnyHolder<Base, 64> otherType(bcos::InPlace<DerivedB>{});
    moved = std::move(otherType);
    BOOST_CHECK_EQUAL(moved->value(), 100);

    // Escape hatches to owning pointers.
    AnyHolder<Base, 64> toShareFrom(bcos::InPlace<DerivedA>{}, 9);
    std::shared_ptr<Base> shared = std::move(toShareFrom).toShared();
    BOOST_CHECK_EQUAL(shared->value(), 9);

    AnyHolder<Base, 64> toUniqueFrom(bcos::InPlace<DerivedA>{}, 11);
    std::unique_ptr<Base> unique = std::move(toUniqueFrom).toUnique();
    BOOST_CHECK_EQUAL(unique->value(), 11);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test

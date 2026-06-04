/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Unit tests for AnyHolder
 * @file AnyHolderTest.cpp
 */
#include "bcos-utilities/AnyHolder.h"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
namespace bcos::test
{

// ── Test types ─────────────────────────────────────────────────────

struct Base
{
    virtual ~Base() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    int value = 0;
};

// Copyable + movable (variant A)
struct CopyMoveA : Base
{
    std::string label;
    CopyMoveA() = default;
    explicit CopyMoveA(std::string s) : label(std::move(s)) {}
    CopyMoveA(const CopyMoveA&) = default;
    CopyMoveA(CopyMoveA&&) noexcept = default;
    CopyMoveA& operator=(const CopyMoveA&) = default;
    CopyMoveA& operator=(CopyMoveA&&) noexcept = default;
    [[nodiscard]] std::string name() const override { return "A:" + label; }
};
static_assert(std::is_copy_constructible_v<CopyMoveA>);
static_assert(std::is_move_constructible_v<CopyMoveA>);

// Copyable + movable (variant B — different type for cross-type tests)
struct CopyMoveB : Base
{
    int id = 0;
    CopyMoveB() = default;
    explicit CopyMoveB(int n) : id(n) {}
    CopyMoveB(const CopyMoveB&) = default;
    CopyMoveB(CopyMoveB&&) noexcept = default;
    CopyMoveB& operator=(const CopyMoveB&) = default;
    CopyMoveB& operator=(CopyMoveB&&) noexcept = default;
    [[nodiscard]] std::string name() const override { return "B:" + std::to_string(id); }
};
static_assert(std::is_copy_constructible_v<CopyMoveB>);
static_assert(std::is_move_constructible_v<CopyMoveB>);

// Move-only
struct MoveOnly : Base
{
    std::string label;
    MoveOnly() = default;
    explicit MoveOnly(std::string s) : label(std::move(s)) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
    [[nodiscard]] std::string name() const override { return "MoveOnly:" + label; }
};
static_assert(!std::is_copy_constructible_v<MoveOnly>);
static_assert(std::is_move_constructible_v<MoveOnly>);

BOOST_AUTO_TEST_SUITE(AnyHolderTest)

// ── Move + Copy (AllowMove=true, AllowCopy=true) ──────────────────

BOOST_AUTO_TEST_CASE(copyAndMove)
{
    using H = AnyHolder<Base, 64, true, true>;

    // default-constructed is empty
    H empty;
    BOOST_CHECK(!empty);
    BOOST_CHECK(!!empty == false);

    // InPlace construction
    H h1(InPlace<CopyMoveA>{}, std::string("hello"));
    BOOST_CHECK(h1);
    BOOST_CHECK_EQUAL(h1->name(), "A:hello");

    // copy construction
    H h2(h1);
    BOOST_CHECK(h2);
    BOOST_CHECK_EQUAL(h2->name(), "A:hello");

    // copy assignment
    H h3;
    BOOST_CHECK(!h3);
    h3 = h1;
    BOOST_CHECK(h3);
    BOOST_CHECK_EQUAL(h3->name(), "A:hello");

    // ── cross-type copy: CopyMoveA → CopyMoveB (different vtables) ─
    H hA(InPlace<CopyMoveA>{}, std::string("cross"));
    H hB(InPlace<CopyMoveB>{}, 42);
    BOOST_CHECK_EQUAL(hA->name(), "A:cross");
    BOOST_CHECK_EQUAL(hB->name(), "B:42");

    // copy-assign cross-type: destroy old B, copy-construct A
    hB = hA;
    BOOST_CHECK_EQUAL(hB->name(), "A:cross");

    // copy-construct cross-type
    H hB2(hA);
    BOOST_CHECK_EQUAL(hB2->name(), "A:cross");

    // move construction
    H h4(std::move(h1));
    BOOST_CHECK_EQUAL(h4->name(), "A:hello");
    // h1 is moved-from (vtable still valid, object in moved-from state)
    BOOST_CHECK(h1);  // vtable still set

    // move assignment
    H h5;
    h5 = std::move(h2);
    BOOST_CHECK(h5);
    BOOST_CHECK_EQUAL(h5->name(), "A:hello");

    // ── cross-type move: CopyMoveA ↔ CopyMoveB ──────────────────────
    H hMA(InPlace<CopyMoveA>{}, std::string("mA"));
    H hMB(InPlace<CopyMoveB>{}, 99);
    // move-assign cross-type: destroy old B, move-construct A
    hMB = std::move(hMA);
    BOOST_CHECK_EQUAL(hMB->name(), "A:mA");

    // move-construct cross-type
    H hMA2(InPlace<CopyMoveA>{}, std::string("mA2"));
    H hMB2(std::move(hMA2));
    BOOST_CHECK_EQUAL(hMB2->name(), "A:mA2");

    // move from empty
    H empty2;
    H empty3(std::move(empty2));
    BOOST_CHECK(!empty3);
}

// ── Move-only (AllowMove=true, AllowCopy=false) ────────────────────

BOOST_AUTO_TEST_CASE(moveOnly)
{
    using H = AnyHolder<Base, 64, true, false>;

    H h1(InPlace<MoveOnly>{}, std::string("world"));
    BOOST_CHECK(h1);
    BOOST_CHECK_EQUAL(h1->name(), "MoveOnly:world");

    // move construction
    H h2(std::move(h1));
    BOOST_CHECK(h2);
    BOOST_CHECK_EQUAL(h2->name(), "MoveOnly:world");

    // move assignment
    H h3;
    h3 = std::move(h2);
    BOOST_CHECK(h3);
    BOOST_CHECK_EQUAL(h3->name(), "MoveOnly:world");

    // copy is disallowed at compile time
    static_assert(!std::is_copy_constructible_v<H>);
    static_assert(!std::is_copy_assignable_v<H>);
}

// ── Copy-only (AllowMove=false, AllowCopy=true) ────────────────────

BOOST_AUTO_TEST_CASE(copyOnly)
{
    using H = AnyHolder<Base, 64, false, true>;

    H h1(InPlace<CopyMoveA>{}, std::string("copyonly"));
    BOOST_CHECK(h1);

    // copy
    H h2(h1);
    BOOST_CHECK(h2);
    BOOST_CHECK_EQUAL(h2->name(), "A:copyonly");

    // copy assignment
    H h3;
    h3 = h1;
    BOOST_CHECK(h3);
    BOOST_CHECK_EQUAL(h3->name(), "A:copyonly");

    // ── cross-type copy ────────────────────────────────────────────
    H hA(InPlace<CopyMoveA>{}, std::string("crossA"));
    H hB(InPlace<CopyMoveB>{}, 7);
    hB = hA;
    BOOST_CHECK_EQUAL(hB->name(), "A:crossA");

    // move is disallowed
    static_assert(!std::is_move_constructible_v<H>);
    static_assert(!std::is_move_assignable_v<H>);
}

// ── Neither copy nor move (AllowMove=false, AllowCopy=false) ───────

BOOST_AUTO_TEST_CASE(noCopyNoMove)
{
    using H = AnyHolder<Base, 64, false, false>;

    H h1(InPlace<CopyMoveA>{}, std::string("norights"));
    BOOST_CHECK(h1);

    // can access but not copy/move
    BOOST_CHECK_EQUAL(h1->name(), "A:norights");

    static_assert(!std::is_copy_constructible_v<H>);
    static_assert(!std::is_copy_assignable_v<H>);
    static_assert(!std::is_move_constructible_v<H>);
    static_assert(!std::is_move_assignable_v<H>);
}

// ── toUnique / toShared ───────────────────────────────────────────

BOOST_AUTO_TEST_CASE(toUniqueAndToShared)
{
    using H = AnyHolder<Base, 64, true, true>;

    H h1(InPlace<CopyMoveA>{}, std::string("unique"));
    auto u = std::move(h1).toUnique();
    BOOST_CHECK(u);
    BOOST_CHECK_EQUAL(u->name(), "A:unique");

    H h2(InPlace<CopyMoveA>{}, std::string("shared"));
    auto s = std::move(h2).toShared();
    BOOST_CHECK(s);
    BOOST_CHECK_EQUAL(s->name(), "A:shared");
}

// ── Cross-type move (copyable ↔ move-only) ────────────────────────

BOOST_AUTO_TEST_CASE(crossTypeMove)
{
    // copy is off — MoveOnly is move-only, can't fill copy slots
    using H = AnyHolder<Base, 64, true, false>;

    // move from CopyMoveA into a holder currently containing MoveOnly
    H hSrc(InPlace<CopyMoveA>{}, std::string("src"));
    H hDst(InPlace<MoveOnly>{}, std::string("dst"));
    BOOST_CHECK_EQUAL(hSrc->name(), "A:src");
    BOOST_CHECK_EQUAL(hDst->name(), "MoveOnly:dst");

    hDst = std::move(hSrc);
    BOOST_CHECK_EQUAL(hDst->name(), "A:src");

    // move from MoveOnly into a holder currently containing CopyMoveA
    H hSrc2(InPlace<MoveOnly>{}, std::string("mo"));
    H hDst2(InPlace<CopyMoveA>{}, std::string("cm"));
    hDst2 = std::move(hSrc2);
    BOOST_CHECK_EQUAL(hDst2->name(), "MoveOnly:mo");

    // move-construct cross-type
    H hSrc3(InPlace<MoveOnly>{}, std::string("fromMO"));
    H hDst3(std::move(hSrc3));
    BOOST_CHECK_EQUAL(hDst3->name(), "MoveOnly:fromMO");
}

// ── Destruction tracking ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(destructionTracking)
{
    static int count = 0;
    struct Tracked : Base
    {
        Tracked() { ++count; }
        Tracked(const Tracked&) { ++count; }
        Tracked(Tracked&&) noexcept { ++count; }
        Tracked& operator=(const Tracked&) = default;
        Tracked& operator=(Tracked&&) noexcept = default;
        ~Tracked() override { --count; }
        [[nodiscard]] std::string name() const override { return "tracked"; }
    };

    {
        AnyHolder<Base, 64, true, true> h(InPlace<Tracked>{});
        BOOST_CHECK_EQUAL(count, 1);
    }
    BOOST_CHECK_EQUAL(count, 0);

    // MoveToUnique transfers ownership: the internal object is
    // move/copy-constructed into the unique_ptr, then AnyHolder
    // is destroyed, so net count stays at 1.
    {
        AnyHolder<Base, 64, true, true> h(InPlace<Tracked>{});
        BOOST_CHECK_EQUAL(count, 1);
        auto u = std::move(h).toUnique();
        // toUnique move/copy-constructs (+1), h is still alive.
        BOOST_CHECK_EQUAL(count, 2);
    }
    // After scope: h's original Tracked destroyed, u destroyed
    BOOST_CHECK_EQUAL(count, 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test

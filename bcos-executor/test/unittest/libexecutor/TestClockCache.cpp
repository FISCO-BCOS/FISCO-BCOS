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
 */
/**
 * @brief : unitest for clock tinyCache implementation
 * @author: catli
 * @date: 2021-09-26
 */

#include "../src/dag/ClockCache.h"
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct ClockCacheFixture
{
    ClockCacheFixture()
    {
        // The capacity of this tinyCache is 1.
        // The num of shard of this tinyCache is 1.
        tinyCache = make_shared<ClockCache<int, int>>(1, 0);

        bigCache = make_shared<ClockCache<int, int>>(64, 6);
    }

    shared_ptr<ClockCache<int, int>> tinyCache;
    shared_ptr<ClockCache<int, int>> bigCache;
};


BOOST_FIXTURE_TEST_SUITE(TestClockCache, ClockCacheFixture)

BOOST_AUTO_TEST_CASE(InsertSameKey)
{
    BOOST_CHECK_EQUAL(tinyCache->insert(1, new int(1)), true);
    BOOST_CHECK_EQUAL(tinyCache->insert(1, new int(2)), true);
    auto handle = tinyCache->lookup(1);
    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.value(), 2);
}

BOOST_AUTO_TEST_CASE(HitAndMiss)
{
    auto handle = tinyCache->lookup(100);
    BOOST_CHECK(!handle.isValid());

    BOOST_CHECK_EQUAL(tinyCache->insert(100, new int(101)), true);
    handle = tinyCache->lookup(100);
    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.value(), 101);
    handle = tinyCache->lookup(200);
    BOOST_CHECK(!handle.isValid());
    handle = tinyCache->lookup(300);
    BOOST_CHECK(!handle.isValid());

    handle = tinyCache->lookup(100);
    BOOST_CHECK(handle.isValid());
    // For now, the tinyCache is full and the item in it cannot be
    // replaced due to `handle` reference to it.
    BOOST_CHECK_EQUAL(tinyCache->insert(200, new int(201)), false);
    // Releases tinyCache handle manually, now tinyCache has enough space
    // to insert a new entry.
    handle.release();
    BOOST_CHECK_EQUAL(tinyCache->insert(200, new int(201)), true);
    handle = tinyCache->lookup(100);
    BOOST_CHECK(!handle.isValid());
    handle = tinyCache->lookup(200);
    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.value(), 201);
    handle = tinyCache->lookup(300);
    BOOST_CHECK(!handle.isValid());

    // Pair (200, 201) still exists in tinyCache, but it isn't referenced
    // by any handle, so we can insert new entry to replace it.
    BOOST_CHECK_EQUAL(tinyCache->insert(100, new int(102)), true);
    handle = tinyCache->lookup(100);
    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.value(), 102);
    handle = tinyCache->lookup(200);
    BOOST_CHECK(!handle.isValid());
    handle = tinyCache->lookup(300);
    BOOST_CHECK(!handle.isValid());
}

BOOST_AUTO_TEST_CASE(EvictionPolicy)
{
    BOOST_CHECK(bigCache->insert(100, new int(101)));
    BOOST_CHECK(bigCache->insert(101, new int(102)));
    BOOST_CHECK(bigCache->insert(102, new int(103)));
    BOOST_CHECK(bigCache->insert(103, new int(104)));

    BOOST_CHECK(bigCache->insert(200, new int(201)));
    BOOST_CHECK(bigCache->insert(201, new int(202)));
    BOOST_CHECK(bigCache->insert(202, new int(203)));
    BOOST_CHECK(bigCache->insert(203, new int(204)));

    auto h200 = bigCache->lookup(200);
    auto h201 = bigCache->lookup(201);
    auto h202 = bigCache->lookup(202);
    auto h203 = bigCache->lookup(203);

    BOOST_CHECK(bigCache->insert(300, new int(301)));
    BOOST_CHECK(bigCache->insert(301, new int(302)));
    BOOST_CHECK(bigCache->insert(302, new int(303)));
    BOOST_CHECK(bigCache->insert(303, new int(304)));

    // Insert entries much more than cache capacity.
    bool insertResult = true;
    for (auto i = 0; i < 10000; ++i)
    {
        insertResult = insertResult && bigCache->insert(1000 + i, new int(2000 + i));
    }
    BOOST_CHECK(insertResult);

    // Check whether the entries inserted in the beginning
    // are evicted. Ones without extra ref are evicted and
    // those with are not.
    BOOST_CHECK(!bigCache->lookup(100).isValid());
    BOOST_CHECK(!bigCache->lookup(101).isValid());
    BOOST_CHECK(!bigCache->lookup(102).isValid());
    BOOST_CHECK(!bigCache->lookup(103).isValid());

    BOOST_CHECK(!bigCache->lookup(300).isValid());
    BOOST_CHECK(!bigCache->lookup(301).isValid());
    BOOST_CHECK(!bigCache->lookup(302).isValid());
    BOOST_CHECK(!bigCache->lookup(303).isValid());

    BOOST_CHECK(bigCache->lookup(200).value() == 201);
    BOOST_CHECK(bigCache->lookup(201).value() == 202);
    BOOST_CHECK(bigCache->lookup(202).value() == 203);
    BOOST_CHECK(bigCache->lookup(203).value() == 204);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos

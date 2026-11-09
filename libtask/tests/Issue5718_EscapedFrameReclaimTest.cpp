/*
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
 * @brief Issue #5718: FireCompletion's two resume sites reclaim the escaped frames of a detached
 *        chain that throws out of the resume, and #5720: the deprecated AwaitableValue.h still
 *        names Immediate.
 */
#include <bcos-task/FireAwaitable.h>
#include <bcos-task/Immediate.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/system/error_code.hpp>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <bcos-task/AwaitableValue.h>
static_assert(std::is_same_v<bcos::task::AwaitableValue<int>, bcos::task::Immediate<int>>);
static_assert(std::is_same_v<bcos::task::AwaitableValue<void>, bcos::task::Immediate<void>>);
#pragma GCC diagnostic pop

namespace
{
using Completion = std::function<void(boost::system::error_code)>;

// A detached chain that parks on a FireAwaitable and throws as soon as it is resumed. The
// payload lives in the parked frame, so its use_count tells whether the frame was destroyed.
// The completion handler is move-only; it is parked in a shared_ptr so a std::function can
// carry it out of the initiate callable.
template <class Park>
void launchThrowingChain(std::shared_ptr<int> payload, Park park)
{
    bcos::task::wait([](std::shared_ptr<int> payload, Park park) -> bcos::task::Task<void> {
        co_await bcos::task::makeFireAwaitable<boost::system::error_code>(
            std::move(park), make_error_code(boost::system::errc::operation_canceled));
        throw std::runtime_error("the resumed chain throws");
    }(std::move(payload), std::move(park)));
}

size_t escapedFrames()
{
    return bcos::task::detail::escapedFrameRegistry().frames.size();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Issue5718_EscapedFrameReclaimTest)

// operator(): the async operation completes later, on this thread, and the chain throws out of
// the resume. The completion swallows the exception; the stranded frames must already be gone.
BOOST_AUTO_TEST_CASE(invokedCompletionReclaimsTheEscapedChain)
{
    auto payload = std::make_shared<int>(1);
    Completion complete;
    launchThrowingChain(payload, [&complete](auto completion) {
        auto parked = std::make_shared<decltype(completion)>(std::move(completion));
        complete = [parked](boost::system::error_code ec) { (*parked)(ec); };
    });
    BOOST_REQUIRE(complete);
    BOOST_CHECK_EQUAL(payload.use_count(), 2);  // the parked frame owns one reference

    BOOST_CHECK_NO_THROW(complete({}));

    BOOST_CHECK_EQUAL(escapedFrames(), 0U);
    BOOST_CHECK_EQUAL(payload.use_count(), 1);
}

// ~FireCompletion: the handler is dropped without being invoked (executor torn down), which
// resumes the chain with the initial error; the chain throws out of that resume too.
BOOST_AUTO_TEST_CASE(droppedCompletionReclaimsTheEscapedChain)
{
    auto payload = std::make_shared<int>(1);
    std::function<void()> drop;
    launchThrowingChain(payload, [&drop](auto completion) {
        auto parked = std::make_shared<std::optional<decltype(completion)>>(
            std::in_place, std::move(completion));
        drop = [parked]() { parked->reset(); };
    });
    BOOST_REQUIRE(drop);
    BOOST_CHECK_EQUAL(payload.use_count(), 2);

    BOOST_CHECK_NO_THROW(drop());

    BOOST_CHECK_EQUAL(escapedFrames(), 0U);
    BOOST_CHECK_EQUAL(payload.use_count(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

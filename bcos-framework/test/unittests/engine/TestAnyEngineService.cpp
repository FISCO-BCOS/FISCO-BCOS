/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-framework/engine/AnyEngineService.h"
#include "bcos-framework/engine/EngineService.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using namespace bcos;
using namespace bcos::engine;
using namespace bcos::protocol;

namespace bcos::test
{

/// A minimal mock that satisfies EngineServiceConcept.
/// Used to verify that AnyEngineService correctly delegates through type erasure.
/// Note: AnyEngineService stores the implementation by value (owning semantics),
/// so tests verify delegation via return values rather than internal mock state.
struct MockEngineService
{
    std::vector<std::string> m_capabilities;
    ForkchoiceUpdatedResult m_forkchoiceResult{
        .payloadStatus = PayloadStatus{.status = PayloadValidationStatus::Valid,
            .latestValidHash = std::nullopt,
            .validationError = std::nullopt},
        .payloadId = std::nullopt};
    GetPayloadResult m_getPayloadResult = std::make_unique<GetPayloadData>();
    PayloadStatus m_payloadStatus{.status = PayloadValidationStatus::Valid,
        .latestValidHash = std::nullopt,
        .validationError = std::nullopt};
    std::optional<BlockNumber> m_safeBlockNumber{42};
    std::optional<BlockNumber> m_finalizedBlockNumber{21};

    // Track which arguments were received
    std::optional<PayloadID> m_capturedPayloadId;
    std::optional<std::uint32_t> m_capturedGetPayloadVersion;
    std::optional<NewPayloadRequest> m_capturedNewPayloadRequest;
    std::optional<std::uint32_t> m_capturedNewPayloadVersion;
    bool m_exchangeCapabilitiesCalled{false};
    bool m_updateForkchoiceCalled{false};

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        m_exchangeCapabilitiesCalled = true;
        m_capabilities = std::move(remoteCapabilities);
        co_return m_capabilities;
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState&, const PayloadAttributes*, std::uint32_t)
    {
        m_updateForkchoiceCalled = true;
        co_return m_forkchoiceResult;
    }

    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        m_capturedPayloadId = payloadId;
        m_capturedGetPayloadVersion = version;
        co_return std::make_unique<GetPayloadData>(*m_getPayloadResult);
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version)
    {
        m_capturedNewPayloadRequest = request;
        m_capturedNewPayloadVersion = version;
        co_return m_payloadStatus;
    }

    std::optional<BlockNumber> getSafeBlockNumber() const { return m_safeBlockNumber; }

    std::optional<BlockNumber> getFinalizedBlockNumber() const { return m_finalizedBlockNumber; }
};

/// A non-copyable, non-movable mock that mimics the constraints of the real
/// EngineServiceImpl (which contains non-copyable members).
/// Used to verify that AnyEngineService's in_place_type constructor correctly
/// stores types that are neither copyable nor movable.
struct NonCopyableEngineService
{
    std::mutex m_mutex;  // makes this type non-copyable and non-movable
    std::optional<BlockNumber> m_safeBlockNumber{100};
    std::optional<BlockNumber> m_finalizedBlockNumber{200};

    NonCopyableEngineService() = default;
    NonCopyableEngineService(const NonCopyableEngineService&) = delete;
    NonCopyableEngineService& operator=(const NonCopyableEngineService&) = delete;
    NonCopyableEngineService(NonCopyableEngineService&&) = delete;
    NonCopyableEngineService& operator=(NonCopyableEngineService&&) = delete;

    /// Constructor with extra args to verify variadic forwarding.
    explicit NonCopyableEngineService(
        std::optional<BlockNumber> safe, std::optional<BlockNumber> finalized)
      : m_safeBlockNumber(safe), m_finalizedBlockNumber(finalized)
    {}

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        co_return remoteCapabilities;
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState&, const PayloadAttributes*, std::uint32_t)
    {
        co_return ForkchoiceUpdatedResult{
            .payloadStatus = PayloadStatus{.status = PayloadValidationStatus::Valid,
                .latestValidHash = std::nullopt,
                .validationError = std::nullopt},
            .payloadId = std::nullopt};
    }

    task::Task<GetPayloadResult> getPayload(const PayloadID&, std::uint32_t)
    {
        co_return GetPayloadResult{};
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest&, std::uint32_t)
    {
        co_return PayloadStatus{.status = PayloadValidationStatus::Valid,
            .latestValidHash = std::nullopt,
            .validationError = std::nullopt};
    }

    std::optional<BlockNumber> getSafeBlockNumber() const { return m_safeBlockNumber; }

    std::optional<BlockNumber> getFinalizedBlockNumber() const { return m_finalizedBlockNumber; }
};

/// Compile-time verification that mocks satisfy the EngineServiceConcept.
static_assert(
    EngineServiceConcept<MockEngineService>, "MockEngineService must satisfy EngineServiceConcept");
static_assert(EngineServiceConcept<NonCopyableEngineService>,
    "NonCopyableEngineService must satisfy EngineServiceConcept");

/// Compile-time verification: AnyEngineService is correctly constrained.
static_assert(
    EngineServiceConcept<AnyEngineService>, "AnyEngineService must satisfy EngineServiceConcept");
static_assert(!std::is_copy_constructible_v<AnyEngineService>,
    "AnyEngineService must not be copy-constructible");
static_assert(
    !std::is_copy_assignable_v<AnyEngineService>, "AnyEngineService must not be copy-assignable");
static_assert(
    std::is_move_constructible_v<AnyEngineService>, "AnyEngineService must be move-constructible");
static_assert(
    std::is_move_assignable_v<AnyEngineService>, "AnyEngineService must be move-assignable");

}  // namespace bcos::test

BOOST_AUTO_TEST_SUITE(TestAnyEngineService)

/// Verify AnyEngineService can be constructed with a mock.
BOOST_AUTO_TEST_CASE(constructWithMock)
{
    bcos::test::MockEngineService mock;
    AnyEngineService any(mock);

    BOOST_CHECK(static_cast<bool>(any));
}

/// Verify exchangeCapabilities delegates correctly.
BOOST_AUTO_TEST_CASE(exchangeCapabilitiesDelegates)
{
    bcos::test::MockEngineService mock;
    AnyEngineService any(mock);

    auto result =
        task::syncWait(any.exchangeCapabilities(std::vector<std::string>{"cap1", "cap2"}));

    BOOST_CHECK_EQUAL(result.size(), 2);
    BOOST_CHECK_EQUAL(result[0], "cap1");
    BOOST_CHECK_EQUAL(result[1], "cap2");
}

/// Verify getSafeBlockNumber delegates correctly on a non-const reference.
BOOST_AUTO_TEST_CASE(getSafeBlockNumberDelegates)
{
    bcos::test::MockEngineService mock;
    mock.m_safeBlockNumber = 42;
    AnyEngineService any(mock);

    auto result = any.getSafeBlockNumber();

    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 42);
}

/// Verify getFinalizedBlockNumber delegates correctly on a non-const reference.
BOOST_AUTO_TEST_CASE(getFinalizedBlockNumberDelegates)
{
    bcos::test::MockEngineService mock;
    mock.m_finalizedBlockNumber = 21;
    AnyEngineService any(mock);

    auto result = any.getFinalizedBlockNumber();

    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 21);
}

/// Verify getSafeBlockNumber can be called on a const AnyEngineService reference.
/// This ensures the const qualifier added to the facade convention and method
/// declaration actually works at the call site.
BOOST_AUTO_TEST_CASE(getSafeBlockNumberOnConst)
{
    bcos::test::MockEngineService mock;
    mock.m_safeBlockNumber = 99;
    const AnyEngineService any(mock);

    auto result = any.getSafeBlockNumber();

    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 99);
}

/// Verify getFinalizedBlockNumber can be called on a const AnyEngineService reference.
BOOST_AUTO_TEST_CASE(getFinalizedBlockNumberOnConst)
{
    bcos::test::MockEngineService mock;
    mock.m_finalizedBlockNumber = 55;
    const AnyEngineService any(mock);

    auto result = any.getFinalizedBlockNumber();

    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 55);
}

/// Verify in_place_type construction with a non-copyable, non-movable type.
/// This is the critical path for wrapping real EngineServiceImpl.
BOOST_AUTO_TEST_CASE(constructNonCopyableInPlace)
{
    auto any = AnyEngineService(std::in_place_type<bcos::test::NonCopyableEngineService>);

    BOOST_CHECK(static_cast<bool>(any));

    auto result = any.getSafeBlockNumber();
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 100);

    auto finalized = any.getFinalizedBlockNumber();
    BOOST_CHECK(finalized.has_value());
    BOOST_CHECK_EQUAL(*finalized, 200);
}

/// Verify in_place_type with extra constructor arguments (variadic forwarding).
BOOST_AUTO_TEST_CASE(constructNonCopyableInPlaceWithArgs)
{
    auto any = AnyEngineService(std::in_place_type<bcos::test::NonCopyableEngineService>,
        std::optional<BlockNumber>{300}, std::optional<BlockNumber>{400});

    BOOST_CHECK(static_cast<bool>(any));

    auto result = any.getSafeBlockNumber();
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 300);

    auto finalized = any.getFinalizedBlockNumber();
    BOOST_CHECK(finalized.has_value());
    BOOST_CHECK_EQUAL(*finalized, 400);
}

/// Verify exchangeCapabilities through an in_place_type-constructed AnyEngineService.
BOOST_AUTO_TEST_CASE(exchangeCapabilitiesNonCopyableInPlace)
{
    auto any = AnyEngineService(std::in_place_type<bcos::test::NonCopyableEngineService>);

    auto result = task::syncWait(any.exchangeCapabilities(std::vector<std::string>{"a", "b", "c"}));

    BOOST_CHECK_EQUAL(result.size(), 3);
}

/// Verify that const methods work on an in_place_type-constructed AnyEngineService.
BOOST_AUTO_TEST_CASE(constMethodsOnNonCopyableInPlace)
{
    const auto any = AnyEngineService(std::in_place_type<bcos::test::NonCopyableEngineService>,
        std::optional<BlockNumber>{500}, std::optional<BlockNumber>{600});

    auto safe = any.getSafeBlockNumber();
    BOOST_CHECK(safe.has_value());
    BOOST_CHECK_EQUAL(*safe, 500);

    auto finalized = any.getFinalizedBlockNumber();
    BOOST_CHECK(finalized.has_value());
    BOOST_CHECK_EQUAL(*finalized, 600);
}

/// Verify move construction of AnyEngineService.
BOOST_AUTO_TEST_CASE(moveConstruction)
{
    bcos::test::MockEngineService mock;
    mock.m_safeBlockNumber = 77;
    AnyEngineService any1(mock);

    AnyEngineService any2(std::move(any1));

    BOOST_CHECK(static_cast<bool>(any2));
    BOOST_CHECK(!static_cast<bool>(any1));

    auto result = any2.getSafeBlockNumber();
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 77);
}

/// Verify move assignment of AnyEngineService.
BOOST_AUTO_TEST_CASE(moveAssignment)
{
    bcos::test::MockEngineService mock1;
    mock1.m_safeBlockNumber = 88;
    bcos::test::MockEngineService mock2;
    mock2.m_safeBlockNumber = 99;
    AnyEngineService any1(mock1);
    AnyEngineService any2(mock2);

    any2 = std::move(any1);

    BOOST_CHECK(static_cast<bool>(any2));
    BOOST_CHECK(!static_cast<bool>(any1));

    auto result = any2.getSafeBlockNumber();
    BOOST_CHECK(result.has_value());
    BOOST_CHECK_EQUAL(*result, 88);
}

BOOST_AUTO_TEST_SUITE_END()

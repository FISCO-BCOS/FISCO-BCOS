%{
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionFactory.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h"

using namespace bcos;
using namespace bcos::protocol;

inline std::vector<bcos::protocol::LogEntry> logEntrySpanToVector(gsl::span<const bcos::protocol::LogEntry> view) {
    return {view.begin(), view.end()};
}
inline std::vector<bcos::h256> h256SpanToVector(gsl::span<const bcos::h256> view) {
    return {view.begin(), view.end()};
}
%}

%include "Crypto.i"
%include <stdint.i>
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>

using bcos::protocol::BlockNumber = long;
using bcos::crypto::HashType = bcos::h256;

%shared_ptr(bcos::protocol::Transaction)
%shared_ptr(bcostars::protocol::TransactionImpl)
%shared_ptr(bcos::protocol::TransactionReceipt)
%shared_ptr(bcostars::protocol::TransactionReceiptImpl)
%shared_ptr(bcos::protocol::TransactionFactory)
%shared_ptr(bcostars::protocol::TransactionFactoryImpl)

%template(LogEntryVector) std::vector<bcos::protocol::LogEntry>;
%template(H256Vector) std::vector<bcos::h256>;

%feature("notabstract") bcostars::protocol::TransactionFactoryImpl;

%include "../bcos-framework/bcos-framework/protocol/LogEntry.h"
%include "../bcos-framework/bcos-framework/protocol/Transaction.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionFactory.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceiptFactory.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionFactoryImpl.h"

inline std::vector<bcos::protocol::LogEntry> logEntrySpanToVector(gsl::span<const bcos::protocol::LogEntry> view);
inline std::vector<bcos::h256> h256SpanToVector(gsl::span<const bcos::h256> view);
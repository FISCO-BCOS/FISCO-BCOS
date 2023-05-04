%{
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionFactory.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"

using namespace bcos;
using namespace bcos::protocol;

inline bcos::bytes encodeTransaction(bcos::protocol::Transaction const& transaction) {
    bytes output;
    transaction.encode(output);
    return output;
}
%}

%include <stdint.i>
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%shared_ptr(bcos::protocol::Transaction)
%shared_ptr(bcostars::protocol::TransactionImpl)
%include "../bcos-framework/bcos-framework/protocol/Transaction.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionFactoryImpl.h"

inline bcos::bytes encodeTransaction(bcos::protocol::Transaction const& transaction);
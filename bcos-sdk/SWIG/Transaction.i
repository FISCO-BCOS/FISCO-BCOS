%{
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionFactory.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h"

using namespace bcos;
using namespace bcos::protocol;
%}

%include <stdint.i>
%include <std_shared_ptr.i>
%include <std_string.i>
%include <std_vector.i>
%shared_ptr(bcos::protocol::Transaction)
%shared_ptr(bcostars::protocol::TransactionImpl)
%shared_ptr(bcos::protocol::TransactionReceipt)
%shared_ptr(bcostars::protocol::TransactionReceiptImpl)
%include "../bcos-framework/bcos-framework/protocol/Transaction.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionFactory.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceiptFactory.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionImpl.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
%include "../bcos-tars-protocol/bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
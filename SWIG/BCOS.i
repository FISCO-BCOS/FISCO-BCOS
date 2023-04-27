%module bcos
%{
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionFactory.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
%}

%include "../bcos-framework/bcos-framework/protocol/Transaction.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionFactory.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
%include "../bcos-framework/bcos-framework/protocol/TransactionReceiptFactory.h"
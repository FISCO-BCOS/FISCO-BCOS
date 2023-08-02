%{
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
%}

%include <std_shared_ptr.i>

%shared_ptr(bcos::sdk::Callback)
%feature("director") bcos::sdk::Callback;
%include "../bcos-cpp-sdk/tarsRPC/Handle.h"


%template(HandleTransactionReceipt) bcos::sdk::Handle<bcos::protocol::TransactionReceipt::Ptr>;
%template(HandleLong) bcos::sdk::Handle<long>;
%include "../bcos-cpp-sdk/tarsRPC/RPCClient.h"
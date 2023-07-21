%{
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
%}

%include "../bcos-cpp-sdk/tarsRPC/RPCClient.h"
%template(FutureLong) bcos::sdk::Future<long>;
%template(FutureReceipt) bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr>;
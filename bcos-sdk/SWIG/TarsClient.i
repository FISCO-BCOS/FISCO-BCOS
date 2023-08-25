%{
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
%}

%include <std_vector.i>
%include "../bcos-cpp-sdk/tarsRPC/RPCClient.h"

%template(CharVector) std::vector<char>;
%template(FutureLong) bcos::sdk::Future<long>;
%template(FutureReceipt) bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr>;
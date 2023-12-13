%module(directors="1") bcos

%include exception.i       
%exception {
  try {
    $action
  } catch(std::exception &e) {
    auto str = boost::diagnostic_information(e);
    SWIG_exception(SWIG_RuntimeError, str.c_str());
  }
}

%include "Utilities.i"
%include "Crypto.i"
%include "Transaction.i"
%include "TarsClient.i"
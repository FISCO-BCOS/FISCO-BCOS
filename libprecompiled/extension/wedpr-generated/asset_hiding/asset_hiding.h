#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

extern "C" {

int8_t verify_fulfilled_credit(char* s);

int8_t verify_issued_credit(char* s);

int8_t verify_split_credit(char* s);

int8_t verify_transfer_credit(char* s);

}  // extern "C"

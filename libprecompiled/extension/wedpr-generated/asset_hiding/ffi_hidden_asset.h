#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

extern "C" {

char* get_credit_storage_by_fulfill_argument(char* fulfill_argument_pb);

char* get_current_credit_by_fulfill_argument(char* fulfill_argument_pb);

char* get_current_credit_by_issue_argument(char* issue_argument_pb);

char* get_new_credit_storage1_by_split_request(char* split_request_pb);

char* get_new_credit_storage2_by_split_request(char* split_request_pb);

char* get_new_credit_storage_by_transfer_request(char* transfer_request_pb);

char* get_new_current_credit1_by_split_request(char* split_request_pb);

char* get_new_current_credit2_by_split_request(char* split_request_pb);

char* get_new_current_credit_by_transfer_request(char* transfer_request_pb);

char* get_spent_credit_storage_by_split_request(char* split_request_pb);

char* get_spent_credit_storage_by_transfer_request(char* transfer_request_pb);

char* get_spent_current_credit_by_split_request(char* split_request_pb);

char* get_spent_current_credit_by_transfer_request(char* transfer_request_pb);

char* get_storage_credit_by_issue_argument(char* issue_argument_pb);

int8_t verify_fulfilled_credit(char* fulfill_argument_pb);

int8_t verify_issued_credit(char* issue_argument_pb);

int8_t verify_split_credit(char* split_request_pb);

int8_t verify_transferred_credit(char* transfer_request_pb);

}  // extern "C"

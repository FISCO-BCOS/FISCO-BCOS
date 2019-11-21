#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

struct backtrace_state;

using backtrace_error_callback = void (*)(void* data, const char* msg, int errnum);

using backtrace_full_callback = int (*)(
    void* data, uintptr_t pc, const char* filename, int lineno, const char* function);

using backtrace_syminfo_callback = void (*)(
    void* data, uintptr_t pc, const char* symname, uintptr_t symval, uintptr_t symsize);

extern "C" {

backtrace_state* __rbt_backtrace_create_state(
    const char* _filename, int _threaded, backtrace_error_callback _error, void* _data);

int __rbt_backtrace_pcinfo(backtrace_state* _state, uintptr_t _addr, backtrace_full_callback _cb,
    backtrace_error_callback _error, void* _data);

int __rbt_backtrace_syminfo(backtrace_state* _state, uintptr_t _addr,
    backtrace_syminfo_callback _cb, backtrace_error_callback _error, void* _data);

char* aggregate_decrypted_part_sum(
    char* param_pb, char* decrypted_result_part_storage_pb, char* count_result_sum_pb);

char* aggregate_h_point(char* h_point_share_cstring, char* h_point_sum_cstring);

char* aggregate_vote_sum_response(char* param_pb, char* vote_storage_part_pb, char* vote_sum_pb);

char* get_blank_ballot_from_vote_request(char* request_pb);

char* get_blank_ballot_from_vote_storage(char* vote_storage_pb);

char* get_counter_id_from_decrypted_result_part(char* decrypted_result_part_pb);

char* get_counter_id_from_decrypted_result_part_request(char* decrypted_result_part_request_pb);

char* get_credit_storage_by_fulfill_argument(char* fulfill_argument_pb);

char* get_current_credit_by_fulfill_argument(char* fulfill_argument_pb);

char* get_current_credit_by_issue_argument(char* issue_argument_pb);

char* get_decrypted_result_part_storage_from_decrypted_result_part_request(char* request_pb);

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

char* get_vote_result_from_request(char* vote_result_request_pb);

char* get_vote_storage_from_vote_request(char* request_pb);

int8_t verify_bounded_vote_request(char* param_pb, char* request_pb);

int8_t verify_count_request(
    char* param_pb, char* encrypted_vote_sum_pb, char* counter_share_string, char* request_pb);

int8_t verify_fulfilled_credit(char* fulfill_argument_pb);

int8_t verify_issued_credit(char* issue_argument_pb);

int8_t verify_split_credit(char* split_request_pb);

int8_t verify_transferred_credit(char* transfer_request_pb);

int8_t verify_unbounded_vote_request(char* param_pb, char* request_pb);

int8_t verify_vote_result(
    char* param_pb, char* vote_sum_pb, char* count_result_sum_pb, char* vote_result_request_pb);

}  // extern "C"

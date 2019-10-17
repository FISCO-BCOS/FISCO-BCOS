#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

extern "C" {

char *aggregate_decrypted_part_sum(char *param_pb, char *request_pb, char *counting_result_sum_pb);

char *aggregate_vote_sum_response(char *param_pb, char *request_pb, char *vote_sum_pb);

char *counting_candidates_result(char *param_pb, char *vote_sum_pb, char *counting_result_sum_pb);

char *get_blank_ballot_from_vote_storage(char *vote_storage_pb);

char *get_counter_id_from_decrypted_result_part_request(char *decrypted_result_part_request_pb);

char *get_decrypted_result_part_storage_from_decrypted_result_part_request(char *request_pb);

char *get_vote_storage_from_vote_request(char *request_pb);

int8_t verify_bounded_vote_request(char *param_pb, char *request_pb);

int8_t verify_count_request(char *param_pb,
                            char *encrypted_vote_sum_pb,
                            char *counter_share_string,
                            char *request_pb);

int8_t verify_unbounded_vote_request(char *param_pb, char *request_pb);

} // extern "C"

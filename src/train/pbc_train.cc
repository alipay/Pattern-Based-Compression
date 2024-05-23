/*
 * Copyright 2023 The PBC Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "train/pbc_train.h"

#include <algorithm>
#include <utility>

#include "base/memcpy.h"
#include "common/utils.h"
#include "compress/pbc_fse_compress.h"
#include "compress/pbc_fsst_compress.h"
#include "compress/pbc_only_compress.h"
#include "compress/pbc_zstd_compress.h"

namespace PBC {

const CompressMethod PBC_Train::DEFAULT_COMPRESS_METHOD = CompressMethod::PBC_FSE;
const size_t PBC_Train::DEFAULT_THREAD_NUM = 16;
const size_t PBC_Train::DEFAULT_SYMBOL_SIZE = 256;
const size_t PBC_Train::DEFAULT_BUFFER_SIZE = (1024 * 1024);

PBC_Train::PBC_Train(CompressMethod compress_method, size_t num_threads, size_t symbol_size,
                     size_t buffer_size)
    : compress_method_(compress_method),
      thread_num_(num_threads),
      symbol_size_(symbol_size),
      buffer_size_(buffer_size) {
    if (thread_num_ > 0) {
        thread_pool_ = new ThreadPool(64);
        thread_pool2_ = new ThreadPool(thread_num_);
    }
}

PBC_Train::~PBC_Train() {
    if (thread_num_ > 0) {
        delete thread_pool_;
        delete thread_pool2_;
    }

    for (auto& pattern_info : pattern_infos_) {
        delete[] pattern_info.pattern_buffer;
    }
}

int64_t PBC_Train::ReadPatternFromDataBuffer(int64_t& data_pos, int64_t max_len,
                                             const char* src_buffer, char* dest_buffer,
                                             int data_type) {
    int64_t start_pos = data_pos;
    int32_t pattern_len = 0;
    if (data_type == TYPE_VARCHAR) {  // varchar
        pbc_memcpy(&pattern_len, src_buffer + start_pos, sizeof(int32_t));
        data_pos += sizeof(int32_t);
        pbc_memcpy(dest_buffer, src_buffer + data_pos, pattern_len);
        data_pos += pattern_len;
    } else {  // split by '\n'
        while (start_pos + pattern_len < max_len && src_buffer[start_pos + pattern_len] != '\n') {
            pattern_len++;
        }
        pbc_memcpy(dest_buffer, src_buffer + data_pos, pattern_len);
        data_pos = start_pos + pattern_len + 1;
    }

    return pattern_len;
}

void PBC_Train::LoadData(char* data_buffer, int64_t len, int data_type) {
    data_buffer_ = data_buffer;
    len_ = len;
    data_type_ = data_type;
    int64_t pattern_len_with_escapestr = 0;
    int cluster_i = 0;
    int64_t data_pos = 0;
    int64_t each_input_pattern_len = 0;

    char* each_input_pattern = new char[len_];

    do {
        PatternInfo pattern_info;
        each_input_pattern_len =
            ReadPatternFromDataBuffer(data_pos, len_, data_buffer_, each_input_pattern, data_type_);
        if (each_input_pattern_len == 0) {
            continue;
        }
        pattern_len_with_escapestr = each_input_pattern_len;
        pattern_info.pattern_buffer = AddEscapeChar(each_input_pattern, pattern_len_with_escapestr);
        pattern_info.pattern_len = pattern_len_with_escapestr;
        // cluster id initialization
        pattern_info.cluster_id = cluster_i++;
        // initially each cluster only contains 1 string
        pattern_info.record_num = 1;
        pattern_info.thresholds = INT_MAX;

        // counting the symbol frequency for 1-gram pruning
        std::vector<int> one_gram(symbol_size_, 0);
        pattern_info.char_freq = each_input_pattern_len;
        for (int i = 0; i < each_input_pattern_len; i++) {
            one_gram[static_cast<int32_t>(static_cast<unsigned char>(each_input_pattern[i]))]++;
        }
        pattern_info.one_gram_table = one_gram;
        pattern_infos_.push_back(pattern_info);
    } while (data_pos < len_);

    all_pattern_num_ = static_cast<int>(pattern_infos_.size());
    delete[] each_input_pattern;
}

int PBC_Train::UpdateState(int cur_state, enum Type suf_type, bool isWildcard, int num_a,
                           int num_b) {
    if (suf_type == pat)
        // if current suffix is in pattern, we should count the wildcard for both two clusters
        cur_state = cur_state + num_a + num_b;
    if (!isWildcard)
        // if current suffix is not wildcard, we should count the symbol
        cur_state = cur_state + num_a;
    else
        // if current suffix is wildcard, we should minus the repeated wildcard
        cur_state = cur_state - num_a;
    return cur_state;
}

int PBC_Train::ConstructTables(std::vector<std::vector<Type>>& type_table,
                               std::vector<std::vector<int>>& state_table,
                               std::vector<std::vector<SourcePos>>& trans_sources,
                               const char* str_a, const char* str_b, int len_a, int len_b,
                               int num_a, int num_b, int threshold) {
    type_table[0][0] = pat;
    state_table[0][0] = 0;
    for (int i = 1; i < len_a + 1; i++) {
        // the type of this position should be filling subsequence
        type_table[i][0] = fs;
        // processing the escape string
        if (str_a[i - 1] == '\\') {
            i++;
            state_table[i][0] = UpdateState(state_table[i - 2][0], type_table[i - 2][0],
                                            /*isWildcard=*/false, num_a, num_b);
        } else {
            if (str_a[i - 1] == '*')
                state_table[i][0] = UpdateState(state_table[i - 1][0], type_table[i - 1][0],
                                                /*isWildcard=*/true, num_a, num_b);
            else
                state_table[i][0] = UpdateState(state_table[i - 1][0], type_table[i - 1][0],
                                                /*isWildcard=*/false, num_a, num_b);
        }
    }
    for (int j = 1; j < len_b + 1; j++) {
        type_table[0][j] = fs;
        // processing the escape string
        if (str_b[j - 1] == '\\') {
            j++;
            state_table[0][j] = UpdateState(state_table[0][j - 2], type_table[0][j - 2],
                                            /*isWildcard=*/false, num_b, num_a);
        } else {
            if (str_b[j - 1] == '*')
                state_table[0][j] = UpdateState(state_table[0][j - 1], type_table[0][j - 1],
                                                /*isWildcard=*/true, num_b, num_a);
            else
                state_table[0][j] = UpdateState(state_table[0][j - 1], type_table[0][j - 1],
                                                /*isWildcard=*/false, num_b, num_a);
        }
    }

    int min_encoding_length = INT_MAX;
    for (int i = 1; i < len_a + 1; i++) {
        int escape_char_num_a = 0;
        if (str_a[i - 1] == '\\') {
            escape_char_num_a++;
            i++;
        }
        int last_pos_a = i - 1 - escape_char_num_a;
        for (int j = 1; j < len_b + 1; j++) {
            int escape_char_num_b = 0;
            if (str_b[j - 1] == '\\') {
                escape_char_num_b++;
                j++;
            }
            int last_pos_b = j - 1 - escape_char_num_b;
            if (str_a[i - 1] == str_b[j - 1] && (str_a[i - 1] != '*' || escape_char_num_a != 0)) {
                // compute the value transfered from state[i][j-1] and state[i-1][j]
                // respectively
                int up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/false, num_a, num_b);
                int left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/false, num_b, num_a);

                int last_pos_value = state_table[last_pos_a][last_pos_b];

                //  the minimal EL is transfered from state[i][j-1], state[i-1][j] and
                //  state[i-1][j-1]
                if (up_value < last_pos_value || left_value < last_pos_value) {
                    type_table[i][j] = fs;

                    if (up_value >= left_value) {
                        state_table[i][j] = left_value;
                        trans_sources[i][j] = uppos;
                    } else {
                        state_table[i][j] = up_value;
                        trans_sources[i][j] = leftpos;
                    }
                } else if (up_value == last_pos_value || left_value == last_pos_value) {
                    state_table[i][j] = last_pos_value;
                    type_table[i][j] = fs;
                    if (up_value >= left_value) {
                        state_table[i][j] = left_value;
                        trans_sources[i][j] = uppos;
                    } else {
                        state_table[i][j] = up_value;
                        trans_sources[i][j] = leftpos;
                    }
                } else {
                    state_table[i][j] = last_pos_value;
                    type_table[i][j] = pat;
                    trans_sources[i][j] = upperleft;
                }
            } else {
                // update the value transfered from state[i][j-1] and state[i-1][j] respectively
                int up_value, left_value;
                if (str_a[i - 1] == '*' && escape_char_num_a == 0)
                    up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/true, num_a, num_b);
                else
                    up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/false, num_a, num_b);
                if (str_b[j - 1] == '*' && escape_char_num_b == 0)
                    left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/true, num_b, num_a);
                else
                    left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/false, num_b, num_a);
                type_table[i][j] = fs;

                // the minimal EL is transfered from state[i][j-1] and state[i-1][j]
                if (up_value >= left_value) {
                    state_table[i][j] = left_value;
                    trans_sources[i][j] = uppos;
                } else {
                    state_table[i][j] = up_value;
                    trans_sources[i][j] = leftpos;
                }
            }
            if (state_table[i][j] < min_encoding_length) min_encoding_length = state_table[i][j];
        }
        if (min_encoding_length >= threshold) return INT_MAX;
    }
    return state_table[len_a][len_b];
}

int PBC_Train::ConstructTablesMultiThreads(std::vector<std::vector<Type>>& type_table,
                                           std::vector<std::vector<int>>& state_table,
                                           std::vector<std::vector<SourcePos>>& trans_sources,
                                           const char* str_a, const char* str_b, int len_a,
                                           int len_b, int num_a, int num_b,
                                           int threshold_id) const {
    type_table[0][0] = pat;
    state_table[0][0] = 0;
    for (int i = 1; i < len_a + 1; i++) {
        // the type of this position should be filling subsequence
        type_table[i][0] = fs;
        // processing the escape string
        if (str_a[i - 1] == '\\') {
            i++;
            state_table[i][0] = UpdateState(state_table[i - 2][0], type_table[i - 2][0],
                                            /*isWildcard=*/false, num_a, num_b);
        } else {
            if (str_a[i - 1] == '*')
                state_table[i][0] = UpdateState(state_table[i - 1][0], type_table[i - 1][0],
                                                /*isWildcard=*/true, num_a, num_b);
            else
                state_table[i][0] = UpdateState(state_table[i - 1][0], type_table[i - 1][0],
                                                /*isWildcard=*/false, num_a, num_b);
        }
    }
    for (int j = 1; j < len_b + 1; j++) {
        type_table[0][j] = fs;
        // processing the escape string
        if (str_b[j - 1] == '\\') {
            j++;
            state_table[0][j] = UpdateState(state_table[0][j - 2], type_table[0][j - 2],
                                            /*isWildcard=*/false, num_b, num_a);
        } else {
            if (str_b[j - 1] == '*')
                state_table[0][j] = UpdateState(state_table[0][j - 1], type_table[0][j - 1],
                                                /*isWildcard=*/true, num_b, num_a);
            else
                state_table[0][j] = UpdateState(state_table[0][j - 1], type_table[0][j - 1],
                                                /*isWildcard=*/false, num_b, num_a);
        }
    }

    int min_encoding_length = INT_MAX;
    for (int i = 1; i < len_a + 1; i++) {
        int escape_char_num_a = 0;
        if (str_a[i - 1] == '\\') {
            escape_char_num_a++;
            i++;
        }
        int last_pos_a = i - 1 - escape_char_num_a;
        for (int j = 1; j < len_b + 1; j++) {
            int escape_char_num_b = 0;
            if (str_b[j - 1] == '\\') {
                escape_char_num_b++;
                j++;
            }
            int last_pos_b = j - 1 - escape_char_num_b;
            if (str_a[i - 1] == str_b[j - 1] && (str_a[i - 1] != '*' || escape_char_num_a != 0)) {
                // compute the value transfered from state[i][j-1] and state[i-1][j]
                // respectively
                int up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/false, num_a, num_b);
                int left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/false, num_b, num_a);

                int last_pos_value = state_table[last_pos_a][last_pos_b];

                //  the minimal EL is transfered from state[i][j-1], state[i-1][j] and
                //  state[i-1][j-1]
                if (up_value < last_pos_value || left_value < last_pos_value) {
                    type_table[i][j] = fs;

                    if (up_value >= left_value) {
                        state_table[i][j] = left_value;
                        trans_sources[i][j] = uppos;
                    } else {
                        state_table[i][j] = up_value;
                        trans_sources[i][j] = leftpos;
                    }
                } else if (up_value == last_pos_value || left_value == last_pos_value) {
                    state_table[i][j] = last_pos_value;
                    type_table[i][j] = fs;
                    if (up_value >= left_value) {
                        state_table[i][j] = left_value;
                        trans_sources[i][j] = uppos;
                    } else {
                        state_table[i][j] = up_value;
                        trans_sources[i][j] = leftpos;
                    }
                } else {
                    state_table[i][j] = last_pos_value;
                    type_table[i][j] = pat;
                    trans_sources[i][j] = upperleft;
                }
            } else {
                // update the value transfered from state[i][j-1] and state[i-1][j] respectively
                int up_value, left_value;
                if (str_a[i - 1] == '*' && escape_char_num_a == 0)
                    up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/true, num_a, num_b);
                else
                    up_value = UpdateState(state_table[last_pos_a][j], type_table[last_pos_a][j],
                                           /*isWildcard=*/false, num_a, num_b);
                if (str_b[j - 1] == '*' && escape_char_num_b == 0)
                    left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/true, num_b, num_a);
                else
                    left_value = UpdateState(state_table[i][last_pos_b], type_table[i][last_pos_b],
                                             /*isWildcard=*/false, num_b, num_a);
                type_table[i][j] = fs;

                // the minimal EL is transfered from state[i][j-1] and state[i-1][j]
                if (up_value >= left_value) {
                    state_table[i][j] = left_value;
                    trans_sources[i][j] = uppos;
                } else {
                    state_table[i][j] = up_value;
                    trans_sources[i][j] = leftpos;
                }
            }
            if (state_table[i][j] < min_encoding_length) min_encoding_length = state_table[i][j];
        }
        if (min_encoding_length >= pattern_infos_[threshold_id].thresholds) return INT_MAX;
    }
    return state_table[len_a][len_b];
}

int PBC_Train::MinEncodingLength(const char* str_a, const char* str_b, int len_a, int len_b,
                                 int num_a, int num_b, int threshold) {
    // store the suffix is in pattern or in filling subsequence
    std::vector<std::vector<Type>> type_table(len_a + 1, std::vector<Type>(len_b + 1));
    // recording the encoding length increment
    std::vector<std::vector<int>> state_table(len_a + 1, std::vector<int>(len_b + 1));
    // store the source of state transition (left, up or upper left)
    std::vector<std::vector<SourcePos>> trans_sources(len_a + 1,
                                                      std::vector<SourcePos>(len_b + 1, esc));
    return ConstructTables(type_table, state_table, trans_sources, str_a, str_b, len_a, len_b,
                           num_a, num_b, threshold);
}

int PBC_Train::MinEncodingLengthMultiThreads(const char* str_a, const char* str_b, int len_a,
                                             int len_b, int num_a, int num_b,
                                             int threshold_id) const {
    // store the suffix is in pattern or in filling subsequence
    std::vector<std::vector<Type>> type_table(len_a + 1, std::vector<Type>(len_b + 1));
    // recording the encoding length increment
    std::vector<std::vector<int>> state_table(len_a + 1, std::vector<int>(len_b + 1));
    // store the source of state transition (left, up or upper left)
    std::vector<std::vector<SourcePos>> trans_sources(len_a + 1,
                                                      std::vector<SourcePos>(len_b + 1, esc));
    return ConstructTablesMultiThreads(type_table, state_table, trans_sources, str_a, str_b, len_a,
                                       len_b, num_a, num_b, threshold_id);
}

int PBC_Train::MergePattern(const char* str_a, const char* str_b, int len_a, int len_b, int num_a,
                            int num_b, std::string& str) {
    // check if str_a and str_b are exactly the same
    // if (len_a == len_b) {
    //     for (uint32_t i = 0; i < len_a; i++) {
    //         if (str_a[i] != str_b[i]) break;
    //         str += str_a[i];
    //         if (i == len_a - 1) return -1;
    //     }
    // }
    // store the suffix is in pattern or in filling subsequence
    std::vector<std::vector<Type>> type_table(len_a + 1, std::vector<Type>(len_b + 1));
    // recording the encoding length increment
    std::vector<std::vector<int>> state_table(len_a + 1, std::vector<int>(len_b + 1));
    // store the source of state transition (left, up or upper left)
    std::vector<std::vector<SourcePos>> trans_sources(len_a + 1,
                                                      std::vector<SourcePos>(len_b + 1, esc));
    ConstructTables(type_table, state_table, trans_sources, str_a, str_b, len_a, len_b, num_a,
                    num_b, INT_MAX);

    // last_type is suffix type of the current pos
    int pos_a = len_a, pos_b = len_b;
    Type last_type = type_table[len_a][len_b];

    if (type_table[len_a][len_b] != pat) {
        str = "*";
    }

    while (pos_a > 0 && pos_b > 0) {
        // only when the state is transfered from upperleft, we add the current suffix to the
        // pattern
        if (trans_sources[pos_a][pos_b] == upperleft) {
            str = str_a[pos_a - 1] + str;
            last_type = pat;
            pos_a--;
            pos_b--;
            // skip the escape string
            while (pos_a > 0 && pos_b > 0 && trans_sources[pos_a][pos_b] == esc) {
                if (last_type == pat) str = "\\" + str;
                pos_a--;
                pos_b--;
            }

        } else if (trans_sources[pos_a][pos_b] == uppos) {
            if (last_type == pat) {
                str = "*" + str;
                last_type = fs;
            }
            pos_b--;
            while (pos_a > 0 && pos_b > 0 && trans_sources[pos_a][pos_b] == esc) {
                if (last_type == pat) str = "\\" + str;
                pos_b--;
            }

        } else if (trans_sources[pos_a][pos_b] == leftpos) {
            if (last_type == pat) {
                str = "*" + str;
                last_type = fs;
            }
            pos_a--;
            while (pos_a > 0 && pos_b > 0 && trans_sources[pos_a][pos_b] == esc) {
                if (last_type == pat) str = "\\" + str;
                pos_a--;
            }
        }
    }

    if (pos_a != pos_b && str[0] != '*') {
        str = "*" + str;
    }
    return state_table[len_a][len_b];
}

char* PBC_Train::AddEscapeChar(char* cstring, int64_t& len) {
    char* result_cstring = new char[3 * len];
    uint32_t cur_pos = 0;
    for (int i = 0; i < len; i++) {
        // add the escape string for wildcard symbol '*'
        if (cstring[i] == '*' || cstring[i] == '\\') {
            result_cstring[cur_pos++] = '\\';
        }
        result_cstring[cur_pos++] = cstring[i];
    }
    result_cstring[cur_pos] = 0;
    len = cur_pos;
    return result_cstring;
}

int PBC_Train::GetMinEncodingLength(int cluster_id1, int cluster_id2, int threshold) const {
    // caculate the the number of common chars
    int value_common = 0;
    for (int k = 0; k < symbol_size_; k++) {
        value_common = value_common + std::min(pattern_infos_[cluster_id1].one_gram_table[k],
                                               pattern_infos_[cluster_id2].one_gram_table[k]);
    }

    if (((pattern_infos_[cluster_id1].char_freq - value_common) *
             pattern_infos_[cluster_id1].record_num +
         (pattern_infos_[cluster_id2].char_freq - value_common) *
             pattern_infos_[cluster_id2].record_num) >= threshold) {
        return INT_MAX;
    }
    return MinEncodingLength(
        pattern_infos_[cluster_id1].pattern_buffer, pattern_infos_[cluster_id2].pattern_buffer,
        pattern_infos_[cluster_id1].pattern_len, pattern_infos_[cluster_id2].pattern_len,
        pattern_infos_[cluster_id1].record_num, pattern_infos_[cluster_id2].record_num, threshold);
}

int PBC_Train::GetMinEncodingLengthMultiThreads(int cluster_id1, int cluster_id2) {
    // caculate the the number of common chars
    int value_common = 0;
    for (int k = 0; k < symbol_size_; k++) {
        value_common = value_common + std::min(pattern_infos_[cluster_id1].one_gram_table[k],
                                               pattern_infos_[cluster_id2].one_gram_table[k]);
    }

    if (((pattern_infos_[cluster_id1].char_freq - value_common) *
             pattern_infos_[cluster_id1].record_num +
         (pattern_infos_[cluster_id2].char_freq - value_common) *
             pattern_infos_[cluster_id2].record_num) >= pattern_infos_[cluster_id1].thresholds) {
        return INT_MAX;
    }
    int min_encoding_length = MinEncodingLengthMultiThreads(
        pattern_infos_[cluster_id1].pattern_buffer, pattern_infos_[cluster_id2].pattern_buffer,
        pattern_infos_[cluster_id1].pattern_len, pattern_infos_[cluster_id2].pattern_len,
        pattern_infos_[cluster_id1].record_num, pattern_infos_[cluster_id2].record_num,
        cluster_id1);
    if (min_encoding_length < pattern_infos_[cluster_id1].thresholds) {
        pattern_infos_[cluster_id1].thresholds = min_encoding_length;
    }
    return min_encoding_length;
}

PBC_Train::MinValueKey PBC_Train::GetMinValue(int cluster_id, bool skip_non_original_cluster) {
    MinValueKey result = {INT_MAX, -1};
    int min_value = INT_MAX;
    if (thread_num_ > 0) {
        pattern_infos_[cluster_id].thresholds = INT_MAX;
        std::vector<std::future<int>> min_encoding_length(all_pattern_num_);
        for (int j = cluster_id + 1; j < all_pattern_num_; j++) {
            if (skip_non_original_cluster && pattern_infos_[j].cluster_id != j) {
                continue;
            }
            min_encoding_length[j] = thread_pool2_->SubmitTask(
                &PBC_Train::GetMinEncodingLengthMultiThreads, this, cluster_id, j);
        }

        // wait min_encoding_length task finished and get the min value
        for (int j = cluster_id + 1; j < all_pattern_num_; j++) {
            if (skip_non_original_cluster && pattern_infos_[j].cluster_id != j) {
                continue;
            }

            int value = min_encoding_length[j].get();
            if (value < min_value) {
                result.value = value;
                result.key = j;
                min_value = value;
            }
        }
    } else {
        for (int j = cluster_id + 1; j < all_pattern_num_; j++) {
            if (skip_non_original_cluster && pattern_infos_[j].cluster_id != j) {
                continue;
            }

            int value = GetMinEncodingLength(cluster_id, j, min_value);

            if (value < min_value) {
                result.value = value;
                result.key = j;
                min_value = value;
            }
        }
    }
    return result;
}

void PBC_Train::ComputeTotalMinValueTable() {
    PBC_LOG(INFO) << "------------ compute minimal encoding length ------------" << std::endl;
    PBC_LOG(INFO) << "init pattern count:" << all_pattern_num_ << std::endl;
    PBC_LOG(INFO) << "---------------------------------------------------------" << std::endl
                  << std::endl;

    int per_num = all_pattern_num_ >= 100 ? all_pattern_num_ / 100 : 1;
    if (thread_num_ > 0) {
        std::vector<std::future<MinValueKey>> min_value_futures;
        for (int i = 0; i < all_pattern_num_ - 1; i++) {
            min_value_futures.push_back(
                thread_pool_->SubmitTask(&PBC_Train::GetMinValue, this, i, false));
        }
        // wait all tasks finished
        for (int i = 0; i < all_pattern_num_ - 1; i++) {
            if (i % per_num == 0) {
                PBC_LOG(DETAIL) << "current compute MEL progress: " << i << "/" << all_pattern_num_
                                << std::endl;
            }
            pattern_infos_[i].min_value_key = min_value_futures[i].get();
        }
    } else {
        for (int i = 0; i < all_pattern_num_ - 1; i++) {
            if (i % per_num == 0) {
                PBC_LOG(DETAIL) << "current compute MEL progress: " << i << "/" << all_pattern_num_
                                << std::endl;
            }
            pattern_infos_[i].min_value_key = GetMinValue(i, false);
        }
    }
}

void PBC_Train::GetClosestCluster(int& cluster_id1, int& cluster_id2) const {
    cluster_id1 = cluster_id2 = -1;
    int cur_min_value = INT_MAX;
    for (int i = 0; i < all_pattern_num_ - 1; i++) {
        if (pattern_infos_[i].cluster_id != i) continue;
        if (pattern_infos_[i].min_value_key.value < cur_min_value) {
            cluster_id1 = i;
            cluster_id2 = pattern_infos_[i].min_value_key.key;
            cur_min_value = pattern_infos_[i].min_value_key.value;
        }
    }
}

void PBC_Train::UpdateMinValueTable(int cluster_id, int changed_cluster_id1,
                                    int changed_cluster_id2) {
    // only update when the cluster cluster_id and its corresponding cluster with minimal EL is a
    // merged cluster
    if (pattern_infos_[cluster_id].min_value_key.key == changed_cluster_id1 ||
        pattern_infos_[cluster_id].min_value_key.key == changed_cluster_id2) {
        pattern_infos_[cluster_id].min_value_key =
            GetMinValue(cluster_id, /*skip_non_original_cluster=*/true);
    } else {
        if (cluster_id < changed_cluster_id1) {
            int value = GetMinEncodingLength(cluster_id, changed_cluster_id1,
                                             pattern_infos_[cluster_id].min_value_key.value);
            if (value < pattern_infos_[cluster_id].min_value_key.value) {
                pattern_infos_[cluster_id].min_value_key.value = value;
                pattern_infos_[cluster_id].min_value_key.key = changed_cluster_id1;
            }
        }
    }
}

bool PBC_Train::CreateSecondaryEncoderData(char* pattern_buffer, int64_t& pattern_len) {
    switch (compress_method_) {
        case PBC_FSE:
            return CreateFseTableUsingCompressedData(pattern_buffer, pattern_len);
        case PBC_FSST:
            return CreateFsstTableUsingCompressedData(pattern_buffer, pattern_len);
        case PBC_ZSTD:
            return CreateZstdDictUsingCompressedData(pattern_buffer, pattern_len);
        case PBC_ONLY:
            // do nothig
            return true;
        default:
            PBC_LOG(ERROR) << "unknown compress method" << std::endl;
    }
    return false;
}

bool PBC_Train::CreateFseTableUsingCompressedData(char* pattern_buffer, int64_t& pattern_len) {
    std::vector<string> compressed_vec;
    std::vector<int> compressed_len_vec;
    PBC::PBC_Compress* pbc_compress = new PBC_ONLY_Compress(symbol_size_, buffer_size_);
    pbc_compress->ReadData(pattern_buffer, pattern_len);

    int64_t data_pos = 0;
    int64_t each_input_data_len = 0;

    char* each_input_data = new char[len_];
    char* compressed_data = new char[len_];
    char* train_data = new char[len_ + 30000];
    uint train_data_offset = 0;

    do {
        each_input_data_len =
            ReadPatternFromDataBuffer(data_pos, len_, data_buffer_, each_input_data, data_type_);
        if (each_input_data_len == 0) {
            continue;
        }
        size_t compress_result = pbc_compress->CompressUsingPattern(
            each_input_data, each_input_data_len, compressed_data);
        if (!PBC::PBC_isError(compress_result)) {
            pbc_memcpy(train_data + train_data_offset, compressed_data, compress_result);
            train_data_offset += compress_result;
        } else {
            PBC_LOG(ERROR) << "Compress failed when CreateFseTableUsingCompressedData."
                           << std::endl;
            return false;
        }
    } while (data_pos < len_);

    for (int i = 0; i < symbol_size_; i++) {
        train_data[train_data_offset++] = (unsigned char)i;
    }

    uint32_t fse_max = symbol_size_;
    uint32_t fse_tableLog = 12;
    int16_t fse_normTable[symbol_size_];

    unsigned int* fse_countTable = new unsigned int[fse_max + 1];
    PBC_HIST_count(fse_countTable, &fse_max, train_data, train_data_offset);
    fse_tableLog = PBC_FSE_optimalTableLog(fse_tableLog, train_data_offset, fse_max);
    PBC_FSE_normalizeCount(fse_normTable, fse_tableLog, fse_countTable, train_data_offset, fse_max);

    size_t cBSize = PBC_FSE_writeNCount(pattern_buffer + pattern_len, buffer_size_, fse_normTable,
                                        fse_max, fse_tableLog);

    pattern_len += cBSize;

    pattern_buffer[pattern_len] = 0;

    delete pbc_compress;
    delete[] train_data;
    delete[] each_input_data;
    delete[] compressed_data;
    delete[] fse_countTable;
    return true;
}

bool PBC_Train::CreateFsstTableUsingCompressedData(char* pattern_buffer, int64_t& pattern_len) {
    PBC_FSST_Compress* pbc_fsst_compress = new PBC_FSST_Compress(symbol_size_, buffer_size_);
    pbc_fsst_compress->ReadData(pattern_buffer, pattern_len);

    std::vector<string> compressed_vec;
    std::vector<int> compressed_len_vec;
    PBC::PBC_Compress* pbc_compress = new PBC_ONLY_Compress(symbol_size_, buffer_size_);
    pbc_compress->ReadData(pattern_buffer, pattern_len);

    int64_t data_pos = 0;
    int64_t each_input_data_len = 0;

    char* each_input_data = new char[len_];
    char* compressed_data = new char[len_];

    do {
        each_input_data_len =
            ReadPatternFromDataBuffer(data_pos, len_, data_buffer_, each_input_data, data_type_);
        if (each_input_data_len == 0) {
            continue;
        }
        size_t compress_result = pbc_compress->CompressUsingPattern(
            each_input_data, each_input_data_len, compressed_data);
        if (!PBC::PBC_isError(compress_result)) {
            std::string compress_str(compressed_data, compress_result);
            compressed_vec.push_back(compress_str);
            compressed_len_vec.push_back(compress_result);
        } else {
            PBC_LOG(ERROR) << "Compress failed when CreateFsstTableUsingCompressedData."
                           << std::endl;
            return false;
        }

    } while (data_pos < len_);

    std::vector<uint64_t> rowLens, compressedRowLens;
    std::vector<unsigned char*> rowPtrs, compressedRowPtrs;
    rowLens.reserve(compressed_vec.size());
    compressedRowLens.resize(compressed_vec.size());
    rowPtrs.reserve(compressed_vec.size());
    compressedRowPtrs.resize(compressed_vec.size() + 1);
    for (auto& d : compressed_vec) {
        rowLens.push_back(d.size());
        rowPtrs.push_back(reinterpret_cast<unsigned char*>(const_cast<char*>(d.data())));
    }

    auto enc = (pbc_fsst_create(compressed_vec.size(), rowLens.data(), rowPtrs.data(), false));
    char* fsst_encoder_buffer = nullptr;
    auto cBSize = pbc_fsst_compress->serializeEncoder(enc, &fsst_encoder_buffer);
    pbc_memcpy(pattern_buffer + pattern_len, fsst_encoder_buffer, cBSize);
    pattern_len += cBSize;
    pattern_buffer[pattern_len] = 0;

    delete pbc_fsst_compress;
    delete pbc_compress;
    delete[] each_input_data;
    delete[] compressed_data;
    delete[] fsst_encoder_buffer;
    return true;
}

bool PBC_Train::CreateZstdDictUsingCompressedData(char* pattern_buffer, int64_t& pattern_len) {
    std::vector<string> compressed_vec;
    std::vector<size_t> compressed_len_vec;
    PBC::PBC_Compress* pbc_compress = new PBC_ONLY_Compress(symbol_size_, buffer_size_);
    pbc_compress->ReadData(pattern_buffer, pattern_len);

    int64_t data_pos = 0;
    int64_t each_input_data_len = 0, samples_size = 0;

    char* each_input_data = new char[len_];
    char* compressed_data = new char[len_];

    do {
        each_input_data_len =
            ReadPatternFromDataBuffer(data_pos, len_, data_buffer_, each_input_data, data_type_);
        if (each_input_data_len == 0) {
            continue;
        }
        size_t compress_result = pbc_compress->CompressUsingPattern(
            each_input_data, each_input_data_len, compressed_data);
        if (!PBC::PBC_isError(compress_result)) {
            std::string compress_str(compressed_data, compress_result);
            compressed_vec.push_back(compress_str);
            compressed_len_vec.push_back(compress_result);
            samples_size += compress_result;
        } else {
            PBC_LOG(ERROR) << "Compress failed when CreateZstdDictUsingCompressedData."
                           << std::endl;
            return false;
        }

    } while (data_pos < len_);

    char* samples_buffer = new char[samples_size];
    size_t sb_writer = 0;
    size_t samples_num = compressed_vec.size();
    size_t* samples_len = new size_t[samples_num];
    int actual_num = 0;

    for (uint i = 0; i < samples_num; i++) {
        if (compressed_len_vec[i] == 0) continue;
        memcpy(samples_buffer + sb_writer, &(compressed_vec[i][0]),
               compressed_len_vec[i] * sizeof(char));
        sb_writer += compressed_len_vec[i] * sizeof(char);
        samples_len[actual_num] = compressed_len_vec[i];
        actual_num++;
    }

    char* dict_buffer = new char[DEFAULT_ZSTD_DICT_SIZE * 100];
    size_t cBSize = ZDICT_trainFromBuffer(dict_buffer, DEFAULT_ZSTD_DICT_SIZE, samples_buffer,
                                          samples_len, actual_num);

    memcpy(pattern_buffer + pattern_len, dict_buffer, cBSize * sizeof(char));
    pattern_len += cBSize;
    pattern_buffer[pattern_len] = 0;

    delete pbc_compress;
    delete[] samples_len;
    delete[] each_input_data;
    delete[] compressed_data;
    delete[] samples_buffer;
    delete[] dict_buffer;
    return true;
}

int64_t PBC_Train::TrainPattern(int k, char** pattern_buffer) {
    PreTrain();

    double ComputeTotalMinValueTable_time = 0.0, MergePattern_time = 0.0,
           UpdateMinValueTable_time = 0.0, GetMinValue_time = 0.0;
    auto ComputeTotalMinValueTable_start_time = std::chrono::steady_clock::now();
    ComputeTotalMinValueTable();
    auto ComputeTotalMinValueTable_end_time = std::chrono::steady_clock::now();
    ComputeTotalMinValueTable_time +=
        std::chrono::duration<double>(ComputeTotalMinValueTable_end_time -
                                      ComputeTotalMinValueTable_start_time)
            .count();
    PBC_LOG(INFO) << "ComputeTotalMinValueTable_time=" << ComputeTotalMinValueTable_time
                  << std::endl;
    int end_num = all_pattern_num_;

    int train_perc_count = 0;
    int64_t report_num = (end_num - k) / 100;
    int64_t itr_count = 0;

    PBC_LOG(INFO) << "------------ merge pattern ---------------" << std::endl;
    PBC_LOG(INFO) << "init pattern count:" << end_num << std::endl;
    PBC_LOG(INFO) << "target pattern num:" << k << std::endl;
    PBC_LOG(INFO) << "------------------------------------------" << std::endl;

    while (end_num > k) {
        if (itr_count > report_num * train_perc_count) {
            PBC_LOG(DETAIL) << "Pattern training " << train_perc_count
                            << "%. current pattern num: " << end_num << std::endl;
            PBC_LOG(DETAIL) << "UpdateMinValueTable_time=" << UpdateMinValueTable_time
                            << "s,MergePattern_time=" << MergePattern_time
                            << "s,GetMinValue_time=" << GetMinValue_time << "s." << std::endl;
            train_perc_count++;
        }
        itr_count++;
        int cluster_id1, cluster_id2;
        GetClosestCluster(cluster_id1, cluster_id2);
        pattern_infos_[cluster_id2].cluster_id = cluster_id1;

        std::string new_pattern;

        auto MergePattern_start_time = std::chrono::steady_clock::now();
        MergePattern(
            pattern_infos_[cluster_id1].pattern_buffer, pattern_infos_[cluster_id2].pattern_buffer,
            pattern_infos_[cluster_id1].pattern_len, pattern_infos_[cluster_id2].pattern_len,
            pattern_infos_[cluster_id1].record_num, pattern_infos_[cluster_id2].record_num,
            new_pattern);
        auto MergePattern_end_time = std::chrono::steady_clock::now();

        int new_pattern_len = new_pattern.length();
        // update one_gram_table_
        std::vector<int> one_gram(symbol_size_, 0);
        pattern_infos_[cluster_id1].char_freq = new_pattern_len;
        for (int i = 0; i < new_pattern_len; i++) {
            one_gram[static_cast<int32_t>(static_cast<unsigned char>(new_pattern[i]))]++;

            if (new_pattern[i] == '\\' && i > 0 && new_pattern[i - 1] != '\\') {
                one_gram[static_cast<int32_t>(static_cast<unsigned char>(new_pattern[i]))]--;
                pattern_infos_[cluster_id1].char_freq--;
            }
            if (new_pattern[i] == '*' && i > 0 && new_pattern[i - 1] != '\\') {
                one_gram[static_cast<int32_t>(static_cast<unsigned char>(new_pattern[i]))]--;
                pattern_infos_[cluster_id1].char_freq--;
            }
            pattern_infos_[cluster_id1].pattern_buffer[i] = new_pattern[i];
        }
        pattern_infos_[cluster_id1].pattern_buffer[new_pattern_len] = '\0';

        pattern_infos_[cluster_id1].one_gram_table = one_gram;
        pattern_infos_[cluster_id1].pattern_len = new_pattern_len;

        pattern_infos_[cluster_id1].record_num =
            pattern_infos_[cluster_id1].record_num + pattern_infos_[cluster_id2].record_num;

        // UpdateMinValueTable
        auto UpdateMinValueTable_start_time = std::chrono::steady_clock::now();
        if (thread_num_ > 0) {
            std::vector<std::future<void>> update_min_value_future;
            for (int i = 0; i < cluster_id2; i++) {
                if (pattern_infos_[i].cluster_id != i) continue;
                if (i == cluster_id1) continue;
                update_min_value_future.push_back(thread_pool_->SubmitTask(
                    &PBC_Train::UpdateMinValueTable, this, i, cluster_id1, cluster_id2));
            }
            for (int i = 0; i < update_min_value_future.size(); i++) {
                update_min_value_future[i].get();
            }
        } else {
            for (int i = 0; i < cluster_id2; i++) {
                if (pattern_infos_[i].cluster_id != i) continue;
                if (i == cluster_id1) continue;
                UpdateMinValueTable(i, cluster_id1, cluster_id2);
            }
        }
        auto UpdateMinValueTable_end_time = std::chrono::steady_clock::now();

        // update the minmal EL of cluster_id1 and corresponding pattern ID
        auto GetMinValue_start_time = std::chrono::steady_clock::now();
        pattern_infos_[cluster_id1].min_value_key =
            GetMinValue(cluster_id1, /*skip_non_original_cluster=*/true);
        auto GetMinValue_end_time = std::chrono::steady_clock::now();
        end_num--;

        UpdateMinValueTable_time += std::chrono::duration<double>(UpdateMinValueTable_end_time -
                                                                  UpdateMinValueTable_start_time)
                                        .count();
        MergePattern_time +=
            std::chrono::duration<double>(MergePattern_end_time - MergePattern_start_time).count();
        GetMinValue_time +=
            std::chrono::duration<double>(GetMinValue_end_time - GetMinValue_start_time).count();
    }

    int64_t buffer_len = 0;
    int32_t pattern_num = 0;
    int32_t max_pattern_len = 0;
    for (int i = 0; i < all_pattern_num_; i++) {
        if (pattern_infos_[i].cluster_id != i) continue;
        if (pattern_infos_[i].pattern_len > 1 && (pattern_infos_[i].record_num > 1)) {
            pattern_num++;
            max_pattern_len = max(max_pattern_len, pattern_infos_[i].pattern_len);
        }
    }

    *pattern_buffer = new char[((max_pattern_len + 1) * pattern_num) + 4096 * 1024];

    PBC_LOG(INFO) << "actual pattern num : " << pattern_num << std::endl;

    pbc_memcpy((*pattern_buffer) + buffer_len, &pattern_num, sizeof(int32_t));
    buffer_len += sizeof(int32_t);
    for (int i = 0; i < all_pattern_num_; i++) {
        if (pattern_infos_[i].cluster_id != i) continue;
        if (pattern_infos_[i].pattern_len > 1 && (pattern_infos_[i].record_num > 1)) {
            pbc_memcpy((*pattern_buffer) + buffer_len, &(pattern_infos_[i].pattern_len),
                       sizeof(int32_t));
            buffer_len += sizeof(int32_t);
            // PBC_LOG(INFO) << "pattern len : " << all_pattern_len_[i] << std::endl;

            pbc_memcpy((*pattern_buffer) + buffer_len, pattern_infos_[i].pattern_buffer,
                       pattern_infos_[i].pattern_len);
            buffer_len += pattern_infos_[i].pattern_len;
        }
    }

    auto CreateSecondaryEncoderData_start_time = std::chrono::steady_clock::now();
    if (!CreateSecondaryEncoderData((*pattern_buffer), buffer_len)) {
        return -1;
    }
    auto CreateSecondaryEncoderData_end_time = std::chrono::steady_clock::now();
    double CreateSecondaryEncoderData_time =
        std::chrono::duration<double>(CreateSecondaryEncoderData_end_time -
                                      CreateSecondaryEncoderData_start_time)
            .count();
    PBC_LOG(INFO) << "ComputeTotalMinValueTable_time=" << ComputeTotalMinValueTable_time
                  << "s,UpdateMinValueTable_time=" << UpdateMinValueTable_time
                  << "s,MergePattern_time=" << MergePattern_time
                  << "s,GetMinValue_time=" << GetMinValue_time
                  << "s,CreateSecondaryEncoderData_time=" << CreateSecondaryEncoderData_time << "s."
                  << std::endl;
    return buffer_len;
}

void PBC_Train::PreTrain() {
    PBC_LOG(INFO) << "start pretrain: current pattern_num = " << all_pattern_num_ << std::endl;
    auto PreTrain_start_time = std::chrono::steady_clock::now();
    std::unordered_map<PatternInfo, int, PatternInfo::PatternInfoHash,
                       PatternInfo::PatternInfoComparetor>
        pattern_info_map;
    for (const PatternInfo& pattern_info : pattern_infos_) {
        if (pattern_info_map.find(pattern_info) != pattern_info_map.end()) {
            // delete the same pattern
            pattern_info_map[pattern_info]++;
            delete[] pattern_info.pattern_buffer;
        } else {
            pattern_info_map[pattern_info]++;
        }
    }
    pattern_infos_.clear();
    int count = 0;
    for (auto& pattern_info_pair : pattern_info_map) {
        PatternInfo pattern_info = pattern_info_pair.first;
        pattern_info.cluster_id = count;
        pattern_info.record_num = pattern_info_pair.second;
        pattern_infos_.push_back(pattern_info);
        count++;
    }
    all_pattern_num_ = count;
    auto PreTrain_end_time = std::chrono::steady_clock::now();
    double PreTrain_time =
        std::chrono::duration<double>(PreTrain_end_time - PreTrain_start_time).count();
    PBC_LOG(INFO) << "end pretrain: current pattern_num = " << all_pattern_num_
                  << ", cost time = " << PreTrain_time << "s." << std::endl;
}

}  // namespace PBC

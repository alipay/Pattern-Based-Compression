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

#ifndef SRC_TRAIN_PBC_TRAIN_H_
#define SRC_TRAIN_PBC_TRAIN_H_

#include <limits.h>
#include <stdio.h>
#include <time.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "compress/compress_factory.h"
#include "train/thread_pool.h"

namespace PBC {

#define UNUSED(V) ((void)V)

class PBC_Train {
public:
    static const CompressMethod DEFAULT_COMPRESS_METHOD;
    static const size_t DEFAULT_THREAD_NUM;
    static const size_t DEFAULT_SYMBOL_SIZE;
    static const size_t DEFAULT_BUFFER_SIZE;

public:
    explicit PBC_Train(CompressMethod compress_method = DEFAULT_COMPRESS_METHOD,
                       size_t num_threads = DEFAULT_THREAD_NUM,
                       size_t symbol_size = DEFAULT_SYMBOL_SIZE,
                       size_t buffer_size = DEFAULT_BUFFER_SIZE);
    ~PBC_Train();
    // Load train data
    void LoadData(char* data_buffer, int64_t len, int data_type);
    // Train pattern
    int64_t TrainPattern(int k, char** pattern_buffer);

private:
    struct MinValueKey {
        int value;
        int key;
    };

    struct PatternInfo {
        char* pattern_buffer;
        int pattern_len;
        // the state of clusters, cluster_id_[i] != i means cluster i had been merged.
        int cluster_id;
        // the number of records in each pattern
        int record_num;
        // the frequency of each character
        int char_freq;
        // min_value_table_[i] stores the information about the cluster that has
        // the minimal EL increment for each cluster
        MinValueKey min_value_key;
        // the 1-gram std::vector of each pattern
        std::vector<int> one_gram_table;
        std::atomic<int> thresholds;

        PatternInfo() : thresholds(0) {}

        PatternInfo(const PatternInfo& pattern_info) {
            this->pattern_buffer = pattern_info.pattern_buffer;
            this->pattern_len = pattern_info.pattern_len;
            this->cluster_id = pattern_info.cluster_id;
            this->record_num = pattern_info.record_num;
            this->char_freq = pattern_info.char_freq;
            this->min_value_key = pattern_info.min_value_key;
            this->one_gram_table = pattern_info.one_gram_table;
            this->thresholds.store(pattern_info.thresholds.load());
        }

        struct PatternInfoHash {
            size_t operator()(const PatternInfo& pattern_info) const {
                std::string str(pattern_info.pattern_buffer, pattern_info.pattern_len);
                return std::hash<std::string>()(str);
            }
        };

        struct PatternInfoComparetor {
            bool operator()(const PatternInfo& pattern_info1,
                            const PatternInfo& pattern_info2) const {
                return pattern_info1.pattern_len == pattern_info2.pattern_len &&
                       memcmp(pattern_info1.pattern_buffer, pattern_info2.pattern_buffer,
                              pattern_info1.pattern_len) == 0;
            }
        };
    };

    enum Type : unsigned char { pat, fs };
    enum SourcePos : unsigned char { leftpos, uppos, upperleft, esc };

private:
    // Read a pattern from data buffer which is follow format: [varint + data] ... [varint + data]
    int64_t ReadPatternFromDataBuffer(int64_t& data_pos, int64_t max_len, const char* src_buffer,
                                      char* dest_buffer, int data_type);
    // Compute the state transfer
    static int UpdateState(int cur_state, enum Type suf_type, bool isWildcard, int num_a,
                           int num_b);
    // Compute the merged pattern of two patterns
    static int MergePattern(const char* str_a, const char* str_b, int len_a, int len_b, int num_a,
                            int num_b, std::string& str);
    // Adding escape string to initialize strings
    static char* AddEscapeChar(char* str, int64_t& len);
    // Construct tables by dynamic programming and return min encoding length
    static int ConstructTables(std::vector<std::vector<Type>>& type_table,
                               std::vector<std::vector<int>>& state_table,
                               std::vector<std::vector<SourcePos>>& trans_sources,
                               const char* str_a, const char* str_b, int len_a, int len_b,
                               int num_a, int num_b, int threshold);
    // Compute the minimal encoding length of two strings
    static int MinEncodingLength(const char* str_a, const char* str_b, int len_a, int len_b,
                                 int num_a, int num_b, int threshold);

    // Construct tables by dynamic programming and return min encoding length
    int ConstructTablesMultiThreads(std::vector<std::vector<Type>>& type_table,
                                    std::vector<std::vector<int>>& state_table,
                                    std::vector<std::vector<SourcePos>>& trans_sources,
                                    const char* str_a, const char* str_b, int len_a, int len_b,
                                    int num_a, int num_b, int threshold) const;
    // Compute the minimal encoding length of two strings
    int MinEncodingLengthMultiThreads(const char* str_a, const char* str_b, int len_a, int len_b,
                                      int num_a, int num_b, int threshold_id) const;

    // Get minimal encoding length of cluster1 and cluster2
    int GetMinEncodingLength(int cluster_id1, int cluster_id2, int threshold) const;
    int GetMinEncodingLengthMultiThreads(int cluster_id1, int cluster_id2);
    // Get min value of cluster cluster_id
    PBC_Train::MinValueKey GetMinValue(int cluster_id, bool skip_non_original_cluster);
    void ComputeMinValue(int cluster_id, bool skip_non_original_cluster);
    // Get closest cluster
    void GetClosestCluster(int& cluster_id1, int& cluster_id2) const;
    // Compute the minimal encoding length and corresponding cluster for each cluster (the
    // min_value_table_). To avoid duplicate computation, cluster(pattern_id = i) only compare with
    // clusters(pattern_id > i)
    void ComputeTotalMinValueTable();
    // Update the min_value of cluster cluster_id after cluster changed_cluster_id merges other
    // cluster
    void UpdateMinValueTable(int cluster_id, int changed_cluster_id1, int changed_cluster_id2);

    // Create fse table using compressed data of train data compressed by pbc_only
    bool CreateFseTableUsingCompressedData(char* pattern_buffer, int64_t& pattern_len);

    // Create fsst table using compressed data of train data compressed by pbc_only
    bool CreateFsstTableUsingCompressedData(char* pattern_buffer, int64_t& pattern_len);

    // Create zstd dict using compressed data of train data compressed by pbc_only
    bool CreateZstdDictUsingCompressedData(char* pattern_buffer, int64_t& pattern_len);

    // Create secondary encoder(fse, fsst, zstd) data
    bool CreateSecondaryEncoderData(char* pattern_buffer, int64_t& pattern_len);

    // Pre operations(such as ) before start train data
    void PreTrain();

private:
    CompressMethod compress_method_;
    int thread_num_;
    ThreadPool* thread_pool_ = nullptr;
    ThreadPool* thread_pool2_ = nullptr;
    size_t symbol_size_;
    size_t buffer_size_;
    char* data_buffer_;
    uint64_t len_;
    std::vector<PatternInfo> pattern_infos_;
    // the initial pattern number
    int32_t all_pattern_num_;
    int data_type_;
};
}  // namespace PBC
#endif  // SRC_TRAIN_PBC_TRAIN_H_

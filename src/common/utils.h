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

#ifndef SRC_COMMON_UTILS_H_
#define SRC_COMMON_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#define TYPE_VARCHAR 0
#define TYPE_RECORD 1

namespace PBC {
enum PBCLogLevel { DETAIL = 0, INFO, ERROR, NONE };
extern PBCLogLevel g_pbc_log_level;
}  // namespace PBC

#define PBC_LOG(level) \
    if (level >= PBC::g_pbc_log_level) std::cerr << "[" << #level "]"

namespace PBC {

// Generate random data with some patterns
int64_t GenerateRandomDataWithPattern(char* buffer, int64_t buffer_len, int64_t data_num,
                                      int64_t data_min_len, int64_t data_max_len,
                                      int64_t pattern_num, int64_t pattern_min_len,
                                      int64_t pattern_max_len, bool contain_null_character);

// Split string by sep
std::vector<std::string> SplitString(std::string str, const std::string& sep);

// Read the original data (with different format) from file
int64_t ReadFile(const char* file_path, char** data_buffer);

/**
 * Parse and unify the data format
 * input_type: TYPE_VARCHAR/TYPE_RECORD
 * TYPE_VARCHAR: len1 + data1 + len2 + data2 + ... + lenn + datan
 * TYPE_RECORD data1 + "\n" + data2 + "\n" + ... + datan
 */
int64_t ReadDataFromBuffer(int32_t input_type, const char* data_buffer, int64_t data_buffer_len,
                           char** parsed_data, int64_t& record_num, int32_t& max_record_len);

// Sample trainset from parsed data
int64_t SamplingFromData(const char* data_buffer, int64_t data_buffer_len, int64_t record_num,
                         char** train_buffer, int64_t train_num);

// Write data to file
void WriteFile(const char* file_path, const char* buffer, int64_t buffer_len);

// Write uint32_t value with varint encoding
void WriteVarint(uint32_t value, uint8_t* ptr, int& ptr_i);

// Read uint32_t value with varint encoding
uint32_t ReadVarint(const uint8_t* data, int& data_i);

// Set pbc log level
void SetPBCLogLevel(int log_level);
}  // namespace PBC

#endif  // SRC_COMMON_UTILS_H_

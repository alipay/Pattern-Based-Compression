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

#include "common/utils.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

#include "base/memcpy.h"

namespace PBC {

PBCLogLevel g_pbc_log_level = PBCLogLevel::INFO;

int64_t GenerateRandomDataWithPattern(char* buffer, int64_t buffer_len, int64_t data_num,
                                      int64_t data_min_len, int64_t data_max_len,
                                      int64_t pattern_num, int64_t pattern_min_len,
                                      int64_t pattern_max_len, bool contain_null_character) {
    if (buffer_len < data_num * data_max_len) {
        PBC_LOG(ERROR) << "The buffer size is too small." << std::endl;
        return -1;
    }
    srand(time(0));
    int64_t buffer_size = 0;
    int rand_char;
    int64_t create_data_num = 0;
    char* pattern_buffer = new char[pattern_max_len];
    int single_pattern_num = data_num / pattern_num;
    for (int i = 0; i < pattern_num; i++) {
        int cur_pattern_len = pattern_min_len + rand() % (pattern_max_len - pattern_min_len);
        int pattern_buffer_pos = 0;
        for (int j = 0; j < cur_pattern_len; j++) {
            rand_char = rand() % 255 + 1;
            while (rand_char == 10) rand_char = rand() % 255 + 1;
            // handle specific characters
            if (j == cur_pattern_len / 4) {
                pattern_buffer[pattern_buffer_pos++] = '\\';
            } else if (j == cur_pattern_len / 3) {
                pattern_buffer[pattern_buffer_pos++] = '*';
            } else if (contain_null_character && j == cur_pattern_len / 2) {
                pattern_buffer[pattern_buffer_pos++] = '\0';
            } else {
                pattern_buffer[pattern_buffer_pos++] = static_cast<char>(rand_char);
            }
        }
        int cur_data_num =
            i == (pattern_num - 1) ? (data_num - create_data_num) : single_pattern_num;
        for (int j = 0; j < cur_data_num; j++) {
            pbc_memcpy(buffer + buffer_size, pattern_buffer, cur_pattern_len);
            buffer_size += cur_pattern_len;
            int cur_data_len = data_min_len + rand() % (data_max_len - data_min_len);
            for (int k = 0; k < cur_data_len - cur_pattern_len; k++) {
                rand_char = rand() % 255 + 1;
                while (rand_char == 10) rand_char = rand() % 255 + 1;
                buffer[buffer_size++] = static_cast<char>(rand_char);
            }
            buffer[buffer_size++] = '\n';
            create_data_num++;
        }
    }
    buffer[buffer_size] = '\0';
    delete[] pattern_buffer;
    return buffer_size;
}

std::vector<std::string> SplitString(std::string str, const std::string& sep) {
    std::vector<std::string> str_vec;
    std::string::size_type pos1, pos2;
    pos2 = str.find(sep);
    pos1 = 0;
    while (std::string::npos != pos2) {
        str_vec.push_back(str.substr(pos1, pos2 - pos1));
        pos1 = pos2 + sep.size();
        pos2 = str.find(sep, pos1);
    }
    if (pos1 != str.length()) str_vec.push_back(str.substr(pos1));
    return str_vec;
}

int64_t ReadFile(const char* file_path, char** data_buffer) {
    struct stat statbuf;
    if (stat(file_path, &statbuf) != 0 || statbuf.st_size <= 0) {
        PBC_LOG(ERROR) << "The input file does not exist or is empty: " << file_path << std::endl;
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    char* temp =
        reinterpret_cast<char*>(mmap(NULL, statbuf.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0));

    *data_buffer = new char[statbuf.st_size + 1];

    pbc_memcpy(*data_buffer, temp, statbuf.st_size);
    return statbuf.st_size;
}

int64_t ReadDataFromBuffer(int32_t input_type, const char* data_buffer, int64_t data_buffer_len,
                           char** parsed_data, int64_t& record_num, int32_t& max_record_len) {
    *parsed_data = new char[data_buffer_len * 2];
    int64_t data_buffer_ptr = 0, write_len = 0;
    record_num = 0;

    if (input_type == TYPE_RECORD) {
        while (data_buffer_ptr < data_buffer_len) {
            int32_t record_len = 0;
            while (data_buffer_ptr + record_len < data_buffer_len &&
                   data_buffer[data_buffer_ptr + record_len] != '\n') {
                record_len++;
            }
            pbc_memcpy((*parsed_data) + write_len, &record_len, sizeof(int32_t));
            write_len += sizeof(int32_t);
            pbc_memcpy((*parsed_data) + write_len, (data_buffer + data_buffer_ptr), record_len);
            write_len += record_len;
            max_record_len = std::max(max_record_len, record_len);
            data_buffer_ptr += (record_len + 1);
            record_num++;
        }
    } else if (input_type == TYPE_VARCHAR) {
        while (data_buffer_ptr < data_buffer_len) {
            int32_t record_len = 0;
            pbc_memcpy(&record_len, data_buffer + data_buffer_ptr, sizeof(int32_t));
            data_buffer_ptr += (sizeof(int32_t) + record_len);
            max_record_len = std::max(max_record_len, record_len);
            record_num++;
        }
        pbc_memcpy((*parsed_data), data_buffer, data_buffer_len);
        write_len = data_buffer_len;
    }
    return write_len;
}

int64_t SamplingFromData(const char* data_buffer, int64_t data_buffer_len, int64_t record_num,
                         char** train_buffer, int64_t train_num) {
    int64_t buffer_ptr = 0, train_buffer_len = 0;
    int64_t sample_step;

    int64_t data_num = record_num, trainset_num = 0;
    PBC_LOG(INFO) << "total data number: " << data_num << std::endl;

    *train_buffer = new char[data_buffer_len];

    sample_step = data_num / train_num;
    if (sample_step < 1) sample_step = 1;

    for (int i = 0; i < data_num; i++) {
        int32_t record_len = 0;
        pbc_memcpy(&record_len, data_buffer + buffer_ptr, sizeof(int32_t));
        buffer_ptr += sizeof(int32_t);
        if (i % sample_step == 0) {
            pbc_memcpy((*train_buffer) + train_buffer_len, &record_len, sizeof(int32_t));
            train_buffer_len += sizeof(int32_t);
            pbc_memcpy((*train_buffer) + train_buffer_len, data_buffer + buffer_ptr, record_len);
            train_buffer_len += record_len;
            trainset_num++;
        }
        buffer_ptr += record_len;
    }

    PBC_LOG(INFO) << "train data number: " << trainset_num << std::endl;

    return train_buffer_len;
}

void WriteFile(const char* file_path, const char* buffer, int64_t buffer_len) {
    std::ofstream output_file;
    output_file.open(file_path, std::ios::out);
    if (!output_file) {
        PBC_LOG(ERROR) << "The output file path does not exist." << std::endl;
        return;
    }

    output_file.write(buffer, buffer_len);
    output_file.close();
}

void WriteVarint(uint32_t value, uint8_t* ptr, int& ptr_i) {
    if (value < 0x80) {
        ptr[0] = static_cast<uint8_t>(value);
        ptr_i += 1;
        return;
    }
    ptr[0] = static_cast<uint8_t>(value | 0x80);
    value >>= 7;
    if (value < 0x80) {
        ptr[1] = static_cast<uint8_t>(value);
        ptr_i += 2;
        return;
    }
    ptr++;
    ptr_i += 2;
    do {
        *ptr = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
        ++ptr;
        ptr_i++;
    } while ((value > 0x80));
    *ptr++ = static_cast<uint8_t>(value);
}

uint32_t ReadVarint(const uint8_t* data, int& data_i) {
    data_i++;
    unsigned int value = data[0];
    if ((value & 0x80) == 0) return value;
    value &= 0x7F;
    data_i++;
    unsigned int chunk = data[1];
    value |= (chunk & 0x7F) << 7;
    if ((chunk & 0x80) == 0) return value;
    chunk = data[2];
    data_i++;
    value |= (chunk & 0x7F) << 14;
    if ((chunk & 0x80) == 0) return value;
    chunk = data[3];
    data_i++;
    value |= (chunk & 0x7F) << 21;
    if ((chunk & 0x80) == 0) return value;
    chunk = data[4];
    data_i++;
    value |= chunk << 28;
    if ((chunk & 0xF0) == 0) return value;
    data_i -= 5;
    return 0;
}

void SetPBCLogLevel(int log_level) { g_pbc_log_level = (PBCLogLevel)log_level; }

}  // namespace PBC

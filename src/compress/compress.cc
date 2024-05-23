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

#include "compress/compress.h"

#include "base/memcpy.h"
#include "common/utils.h"

namespace PBC {

const size_t PBC_Compress::DEFAULT_SYMBOL_SIZE = 256;
const size_t PBC_Compress::DEFAULT_BUFFER_SIZE = (1024 * 1024);

PBC_Compress::PBC_Compress(size_t symbol_size, size_t buffer_size) {
    symbol_size_ = symbol_size;
    buffer_size_ = buffer_size;

    output_buffer_ = new char[buffer_size_];
}

PBC_Compress::~PBC_Compress() {
    delete[] output_buffer_;
    if (hs_db_block_) hs_free_database(hs_db_block_);
    if (hs_scratch_) hs_free_scratch(hs_scratch_);
}

int PBC_Compress::OnMatch(unsigned int id,
                          unsigned long long from,  // NOLINT
                          unsigned long long to,    // NOLINT
                          unsigned int flags, void* ctx) {
    // Our context points to a size_t storing the match count
    size_t* matches = reinterpret_cast<size_t*>(ctx);
    if (pattern_len_list[id] > pattern_len_list[static_cast<int>(*matches)]) {
        (*matches) = id;
    }
    return 0;  // continue matching
}

hs_database_t* PBC_Compress::BuildDatabase(const std::vector<const char*>& expressions,
                                           const std::vector<unsigned>& flags,
                                           const std::vector<unsigned>& ids, unsigned int mode) {
    hs_database_t* db;
    hs_compile_error_t* compile_error;
    hs_error_t err;

    err = hs_compile_multi(expressions.data(), flags.data(), ids.data(), expressions.size(), mode,
                           nullptr, &db, &compile_error);

    if (err != HS_SUCCESS) {
        if (compile_error->expression < 0) {
            // The error does not refer to a particular expression.
            PBC_LOG(ERROR) << "ERROR: " << compile_error->message << std::endl;
        } else {
            PBC_LOG(ERROR) << "ERROR: Pattern '" << expressions[compile_error->expression]
                           << "' failed compilation with error: " << compile_error->message << " "
                           << compile_error->expression << std::endl;
        }
        // As the compile_error pointer points to dynamically allocated memory, if
        // we get an error, we must be sure to release it. This is not
        // necessary when no error is detected.
        hs_free_compile_error(compile_error);
        return nullptr;
    }
    return db;
}

unsigned PBC_Compress::ParseFlags(const std::string& flagsStr) {
    unsigned flags = 0;
    for (const auto& c : flagsStr) {
        switch (c) {
            case 'i':
                flags |= HS_FLAG_CASELESS;
                break;
            case 'm':
                flags |= HS_FLAG_MULTILINE;
                break;
            case 's':
                flags |= HS_FLAG_DOTALL;
                break;
            case 'H':
                flags |= HS_FLAG_SINGLEMATCH;
                break;
            case 'V':
                flags |= HS_FLAG_ALLOWEMPTY;
                break;
            case '8':
                flags |= HS_FLAG_UTF8;
                break;
            case 'W':
                flags |= HS_FLAG_UCP;
                break;
            case 'B':
                flags |= HS_TUNE_FAMILY_IVB;
                break;
            case 'a':
                flags |= HS_CPU_FEATURES_AVX512;
                break;
            case '\r':  // stray carriage-return
                break;
            default:
                PBC_LOG(ERROR) << "Unsupported flag \'" << c << "\'" << std::endl;
                break;
        }
    }
    return flags;
}

/*
    hs_compile_multi requires three parallel arrays containing the patterns,
    flags and ids that we want to work with. To achieve this we use
    vectors and new entries onto each for each valid line of input from
    the pattern file.
    do the actual file reading and std::string handling
    parseFile(filename, patterns, flags, ids);
    Turn our std::vector of strings into a std::vector of char*'s to pass in to
    hs_compile_multi. (This is just using the std::vector of strings as dynamic
    storage.)
*/
bool PBC_Compress::CreateDatabaseFromFile(const std::vector<std::string>& patterns,
                                          const std::vector<unsigned>& flags,
                                          const std::vector<unsigned>& ids,
                                          hs_database_t** db_block) {
    std::vector<const char*> cstrPatterns;
    for (const auto& pattern : patterns) {
        cstrPatterns.push_back(pattern.c_str());
    }

    *db_block = BuildDatabase(cstrPatterns, flags, ids, HS_MODE_BLOCK);
    if (*db_block == nullptr) {
        return false;
    }
    return true;
}

int PBC_Compress::FillingSubsequences(int pattern_id, const std::string& input_string,
                                      char* output_cstring, int input_cstring_len) const {
    const patternInfo& pattern_info = pattern_list_[pattern_id];
    int output_cstring_len = 2;
    const char* pattern_data_cstring = &(pattern_info.data[0]);

    const char* input_cstring = &input_string[0];
    int start_pos = 0;
    for (int pattern_num = 0; pattern_num < pattern_info.num; pattern_num++) {
        int pattern_len = pattern_info.pos[pattern_num + 1] - pattern_info.pos[pattern_num];
        if (pattern_len == 0) {
            // pattern_len=0 only when this pattern char is '*' and it it fisrt/last pattern char
            if (pattern_num != 0 && pattern_num != pattern_info.num - 1) {
                return -1;
            }
            continue;
        }
        int match_pos = input_string.find(pattern_data_cstring + pattern_info.pos[pattern_num],
                                          start_pos, pattern_len);

        if (std::string::npos == match_pos) {
            return -1;
        } else if (match_pos == start_pos) {
            // first pattern part is not needed to write varint
            if (pattern_num > 0) {
                // write varint 0
                output_cstring[output_cstring_len++] = 0;
            }
        } else {
            WriteVarint((uint32_t)(match_pos - start_pos),
                        (unsigned char*)output_cstring + output_cstring_len, output_cstring_len);
            pbc_memcpy(output_cstring + output_cstring_len, input_cstring + start_pos,
                       match_pos - start_pos);

            output_cstring_len += match_pos - start_pos;
        }
        start_pos = match_pos + pattern_len;
    }

    if (pattern_info.pos[pattern_info.num] ==
        pattern_info.pos[pattern_info.num - 1]) {  // last pattern char is ".*"
        if (start_pos < input_cstring_len) {
            WriteVarint((uint32_t)(input_cstring_len - start_pos),
                        (unsigned char*)output_cstring + output_cstring_len, output_cstring_len);
            pbc_memcpy(output_cstring + output_cstring_len, input_cstring + start_pos,
                       input_cstring_len - start_pos);
            output_cstring_len += input_cstring_len - start_pos;
        } else {  // start_pos = input_cstring_len
            output_cstring[output_cstring_len++] = 0;
        }
    } else if (start_pos != input_cstring_len) {
        return -1;
    }
    output_cstring[output_cstring_len] = 0;
    return output_cstring_len;
}

int64_t PBC_Compress::ReadPattern(const char* data) {
    int64_t data_ptr = 0;
    pbc_memcpy(&pattern_num_, data + data_ptr, sizeof(int32_t));
    data_ptr += sizeof(int32_t);

    pattern_list_.resize(pattern_num_);
    pattern_len_list.resize(pattern_num_ + 1);
    patterns_.resize(pattern_num_);
    flags_.resize(pattern_num_);
    ids_.resize(pattern_num_);
    unsigned flag = ParseFlags("Ha");

    for (int32_t pattern_pos = 0; pattern_pos < pattern_num_; pattern_pos++) {
        flags_[pattern_pos] = flag;
        ids_[pattern_pos] = pattern_pos;
        std::string pattern_HS = "";
        pattern_list_[pattern_pos].num = 0;
        pattern_list_[pattern_pos].pos.push_back(0);

        int32_t pattern_len = 0;
        pbc_memcpy(&pattern_len, data + data_ptr, sizeof(int32_t));
        data_ptr += sizeof(int32_t);

        if (data[data_ptr] != '*') pattern_HS += '^';

        int pattern_list_pos = 0;

        const int32_t pat_buf_size = pattern_len + 1;
        char* each_pattern = new char[pat_buf_size];
        pbc_memcpy(each_pattern, data + data_ptr, pattern_len);
        data_ptr += pattern_len;
        for (int32_t i = 0; i < pattern_len; i++) {
            if (each_pattern[i] == '\\') {
                if (i == pattern_len - 1 ||
                    (each_pattern[i + 1] != '\\' && each_pattern[i + 1] != '*')) {
                    return -1;
                }
                if (each_pattern[i + 1] == '\\') {
                    pattern_HS += '\\';
                    pattern_HS += '\\';
                    pattern_list_[pattern_pos].data += '\\';
                    pattern_list_pos++;
                } else {  // each_pattern[i + 1] == '*'
                    pattern_HS += "\\";
                    pattern_HS += "*";
                    pattern_list_[pattern_pos].data += '*';
                    pattern_list_pos++;
                }
                i++;
                continue;
            } else if (each_pattern[i] == '*') {
                pattern_HS += ".*";
                pattern_list_[pattern_pos].num++;
                pattern_list_[pattern_pos].pos.push_back(pattern_list_pos);
                continue;
            }
            // processing specialchars: adding \\ to escape
            if (IsSpecialChar(each_pattern[i])) {
                pattern_HS += '\\';
            }
            // processing the '\0': '\0' -> '\\'+'0'
            if (each_pattern[i] == '\0') {
                pattern_HS += '\\';
                pattern_HS += '0';
                pattern_list_[pattern_pos].data += each_pattern[i];
                pattern_list_pos++;
                continue;
            }
            pattern_HS += each_pattern[i];
            pattern_list_[pattern_pos].data += each_pattern[i];
            pattern_list_pos++;
        }
        delete[] each_pattern;

        // last pattern char must be ".*"
        if (pattern_HS.back() != '*' ||
            (pattern_HS.length() > 1 && pattern_HS[pattern_HS.length() - 2] != '.')) {
            pattern_HS += ".*";
            pattern_list_[pattern_pos].num++;
            pattern_list_[pattern_pos].pos.push_back(pattern_list_pos);
        }
        patterns_[pattern_pos] = pattern_HS;
        pattern_list_[pattern_pos].num++;
        pattern_list_[pattern_pos].pos.push_back(pattern_list_pos);
        pattern_len_list[pattern_pos] =
            pattern_list_[pattern_pos].data.length() - pattern_list_[pattern_pos].num;
    }

    pattern_len_list[pattern_num_] = 0;
    return data_ptr;
}

bool PBC_Compress::ReadData(const char* data, int64_t len) {
    int64_t data_ptr = ReadPattern(data);
    if (data_ptr < 0) {
        PBC_LOG(ERROR) << "ERROR: read pattern failed." << std::endl;
        return false;
    }

    if (!CreateDatabaseFromFile(patterns_, flags_, ids_, &hs_db_block_)) {
        PBC_LOG(ERROR) << "ERROR: create hyperscan database failed." << std::endl;
        return false;
    }

    hs_error_t err = hs_alloc_scratch(hs_db_block_, &hs_scratch_);

    if (err != HS_SUCCESS) {
        PBC_LOG(ERROR) << "ERROR: could not allocate scratch space." << std::endl;
        return false;
    }

    if (len == data_ptr) {
        return true;
    }

    BuildSecondaryEncoder(data, len, data_ptr);
    return true;
}

size_t PBC_Compress::CompressUsingPattern(const char* input_cstring, size_t input_cstring_len,
                                          char* output_cstring) {
    size_t match_pattern_id = pattern_num_;

    hs_error_t err = hs_scan(hs_db_block_, input_cstring, input_cstring_len, 0, hs_scratch_,
                             OnMatch, &match_pattern_id);

    if (err != HS_SUCCESS) {
        PBC_LOG(ERROR) << "ERROR: Unable to scan packet. Error code:" << err << std::endl;
        return PBC_ERROR(PBC_error_compress_failed);
    }
    if (match_pattern_id != pattern_num_) {  // find match pattern
        // 2 bytes to store the id of matched pattern
        output_cstring[1] = match_pattern_id / symbol_size_;
        output_cstring[2] = match_pattern_id % symbol_size_;

        std::string input_string(input_cstring, input_cstring_len);

        int len = FillingSubsequences(match_pattern_id, input_string, output_cstring + 1,
                                      input_cstring_len);
        if (len < 0) {
            PBC_LOG(ERROR) << "ERROR: FillingSubsequences failed." << std::endl;
            return PBC_ERROR(PBC_error_compress_failed);
        }
        size_t cBSize =
            ApplySecondaryEncoding(output_cstring + 1, len, output_buffer_, buffer_size_);

        if (cBSize == 0 || cBSize >= len) {
            output_cstring[0] = CompressTypeFlag::COMPRESS_PBC_ONLY;
            return len + 1;
        }
        output_cstring[0] = CompressTypeFlag::COMPRESS_PBC_COMBINED;
        pbc_memcpy(output_cstring + 1, output_buffer_, cBSize);
        output_cstring[cBSize + 1] = 0;
        return cBSize + 1;
    } else {  // not find match pattern
        size_t cBSize =
            ApplySecondaryEncoding(input_cstring, input_cstring_len, output_buffer_, buffer_size_);
        if (cBSize == 0 || cBSize >= input_cstring_len) {
            output_cstring[0] = CompressTypeFlag::COMPRESS_NOT_COMPRESS;
            pbc_memcpy(output_cstring + 1, input_cstring, input_cstring_len);
            output_cstring[input_cstring_len + 1] = 0;
            return input_cstring_len + 1;
        }
        output_cstring[0] = CompressTypeFlag::COMPRESS_SECONDARY_ONLY;
        pbc_memcpy(output_cstring + 1, output_buffer_, cBSize);
        output_cstring[cBSize + 1] = 0;
        return cBSize + 1;
    }
}

size_t PBC_Compress::DecompressUsingPattern(const char* input_cstring, int input_cstring_len,
                                            char* output_cstring) {
    // At least two bytes
    if (input_cstring_len < 2) {
        return PBC_ERROR(PBC_error_decompress_failed);
    }

    if (input_cstring[0] == CompressTypeFlag::COMPRESS_NOT_COMPRESS) {
        pbc_memcpy(output_cstring, input_cstring + 1, input_cstring_len - 1);
        output_cstring[input_cstring_len] = 0;
        return input_cstring_len - 1;
    }

    if (input_cstring[0] != CompressTypeFlag::COMPRESS_PBC_ONLY &&
        input_cstring[0] != CompressTypeFlag::COMPRESS_SECONDARY_ONLY &&
        input_cstring[0] != CompressTypeFlag::COMPRESS_PBC_COMBINED) {
        return PBC_ERROR(PBC_error_decompress_failed);
    }

    // compress using pbc at least thress bytes: fist bytes stores compress type and two bytes store
    // pattern id
    if (input_cstring_len < 3 && input_cstring[0] != COMPRESS_SECONDARY_ONLY) {
        return PBC_ERROR(PBC_error_decompress_failed);
    }
    size_t cBSize = 0;

    uint32_t varint_num;
    if (input_cstring[0] == COMPRESS_SECONDARY_ONLY) {
        cBSize = ApplySecondaryDecoding(input_cstring + 1, input_cstring_len - 1, output_cstring,
                                        buffer_size_);
        if (cBSize == 0) {
            return PBC_ERROR(PBC_error_decompress_failed);
        }
        return cBSize;
    } else if (input_cstring[0] == COMPRESS_PBC_COMBINED) {
        cBSize = ApplySecondaryDecoding(input_cstring + 1, input_cstring_len - 1, output_buffer_,
                                        buffer_size_);
        if (cBSize == 0) {
            return PBC_ERROR(PBC_error_decompress_failed);
        }
        output_buffer_[cBSize] = 0;
        input_cstring_len = cBSize;
    } else {
        pbc_memcpy(output_buffer_, input_cstring + 1, input_cstring_len - 1);
    }

    int pattern_id =
        (static_cast<int32_t>(static_cast<unsigned char>(output_buffer_[0])) * symbol_size_ +
         static_cast<int32_t>(static_cast<unsigned char>(output_buffer_[1])));
    const patternInfo& pattern_info = pattern_list_[pattern_id];

    if (input_cstring_len == 3) {
        pbc_memcpy(output_cstring, pattern_info.data.c_str(), pattern_info.data.length());
        return pattern_info.data.length();
    }
    int output_cstring_len = 0;
    const char* common_str = &pattern_info.data[0];

    int output_buffer_pos = 2;  // first two bytes store pattern id
    // first pattern char is ".*"
    if (pattern_info.pos[1] - pattern_info.pos[0] == 0) {
        varint_num =
            ReadVarint((unsigned char*)(output_buffer_ + output_buffer_pos), output_buffer_pos);
        pbc_memcpy(output_cstring + output_cstring_len, output_buffer_ + output_buffer_pos,
                   varint_num);
        output_cstring_len += varint_num;
        output_buffer_pos = output_buffer_pos + varint_num;
    }

    for (int i = 0; i < pattern_info.num; i++) {
        if (pattern_info.pos[i + 1] - pattern_info.pos[i] == 0) {
            // pattern_len=0 only when this pattern char is ".*" and it it fisrt/last pattern char
            if (i != 0 && i != pattern_info.num - 1) {
                return PBC_ERROR(PBC_error_decompress_failed);
            }
            continue;
        }
        pbc_memcpy(output_cstring + output_cstring_len, common_str + pattern_info.pos[i],
                   pattern_info.pos[i + 1] - pattern_info.pos[i]);
        output_cstring_len += pattern_info.pos[i + 1] - pattern_info.pos[i];
        if (i != pattern_info.num - 1) {
            varint_num =
                ReadVarint((unsigned char*)(output_buffer_ + output_buffer_pos), output_buffer_pos);
            pbc_memcpy(output_cstring + output_cstring_len, output_buffer_ + output_buffer_pos,
                       varint_num);
            output_cstring_len += varint_num;
            output_buffer_pos = output_buffer_pos + varint_num;
        }
    }
    output_cstring[output_cstring_len] = 0;
    return output_cstring_len;
}

bool PBC_Compress::IsSpecialChar(char ch) {
    char special_chars[] = {'$', '(', ')', '[', ']', '{', '}', '?', '^',
                            '.', '+', '*', '|', '-', '=', ':', '/'};
    for (auto& special_char : special_chars) {
        if (ch == special_char) {
            return true;
        }
    }
    return false;
}

size_t PBC_Compress::CompressUsingPatternWithLength(const char* input_cstring,
                                                    size_t input_cstring_len,
                                                    char* output_cstring) {
    size_t match_pattern_id = pattern_num_;

    hs_error_t err = hs_scan(hs_db_block_, input_cstring, input_cstring_len, 0, hs_scratch_,
                             OnMatch, &match_pattern_id);

    if (err != HS_SUCCESS) {
        PBC_LOG(ERROR) << "ERROR: Unable to scan packet. Error code:" << err << std::endl;
        return PBC_ERROR(PBC_error_compress_failed);
    }
    if (match_pattern_id != pattern_num_) {  // find match pattern
        // 2 bytes to store the id of matched pattern
        output_cstring[0] = match_pattern_id / symbol_size_;
        output_cstring[1] = match_pattern_id % symbol_size_;

        std::string input_string(input_cstring, input_cstring_len);

        int len =
            FillingSubsequences(match_pattern_id, input_string, output_cstring, input_cstring_len);
        if (len < 0) {
            PBC_LOG(ERROR) << "ERROR: FillingSubsequences failed." << std::endl;
            return PBC_ERROR(PBC_error_compress_failed);
        }
        return len;
    } else {  // not find match pattern
        output_cstring[0] = pattern_num_ / symbol_size_;
        output_cstring[1] = pattern_num_ % symbol_size_;
        int output_cstring_len = 2;
        WriteVarint((uint32_t)(input_cstring_len),
                    (unsigned char*)output_cstring + output_cstring_len, output_cstring_len);
        pbc_memcpy(output_cstring + output_cstring_len, input_cstring, input_cstring_len);
        output_cstring_len += input_cstring_len;
        return output_cstring_len;
    }
}

size_t PBC_Compress::DecompressUsingPatternWithLength(const char* input_cstring,
                                                      int input_cstring_len, char* output_cstring) {
    // at least two bytes to store pattern id
    if (input_cstring_len < 2) {
        return PBC_ERROR(PBC_error_decompress_failed);
    }
    int pattern_id =
        (static_cast<int32_t>(static_cast<unsigned char>(input_cstring[0])) * symbol_size_ +
         static_cast<int32_t>(static_cast<unsigned char>(input_cstring[1])));
    if (pattern_id == pattern_num_) {
        int variant_length = 0;
        size_t output_cstring_len = ReadVarint((unsigned char*)(input_cstring + 2), variant_length);
        if (2 + variant_length + output_cstring_len >
            input_cstring_len + 1) {  // current data is incomplete
            return PBC_ERROR(PBC_error_decompress_failed);
        }
        pbc_memcpy(output_cstring, input_cstring + 2 + variant_length, output_cstring_len);
        return output_cstring_len;
    } else if (pattern_id > pattern_num_) {
        return PBC_ERROR(PBC_error_decompress_failed);
    }

    const patternInfo& pattern_info = pattern_list_[pattern_id];

    int output_cstring_len = 0;
    const char* common_str = &pattern_info.data[0];

    int output_buffer_pos = 2;  // first two bytes store pattern id
    uint32_t varint_num = 0;
    // first pattern char is ".*"
    if (pattern_info.pos[1] - pattern_info.pos[0] == 0) {
        varint_num =
            ReadVarint((unsigned char*)(input_cstring + output_buffer_pos), output_buffer_pos);
        if (output_buffer_pos + varint_num > input_cstring_len + 1) {  // current data is incomplete
            return PBC_ERROR(PBC_error_decompress_failed);
        }
        pbc_memcpy(output_cstring + output_cstring_len, input_cstring + output_buffer_pos,
                   varint_num);
        output_cstring_len += varint_num;
        output_buffer_pos = output_buffer_pos + varint_num;
    }

    for (int i = 0; i < pattern_info.num; i++) {
        if (pattern_info.pos[i + 1] - pattern_info.pos[i] == 0) {
            // pattern_len=0 only when this pattern char is '*' and it it fisrt/last pattern char
            if (i != 0 && i != pattern_info.num - 1) {
                return PBC_ERROR(PBC_error_decompress_failed);
            }
            continue;
        }
        pbc_memcpy(output_cstring + output_cstring_len, common_str + pattern_info.pos[i],
                   pattern_info.pos[i + 1] - pattern_info.pos[i]);
        output_cstring_len += pattern_info.pos[i + 1] - pattern_info.pos[i];
        if (i != pattern_info.num - 1) {
            varint_num =
                ReadVarint((unsigned char*)(input_cstring + output_buffer_pos), output_buffer_pos);
            if (output_buffer_pos + varint_num > input_cstring_len + 1 ||
                output_buffer_pos > input_cstring_len) {  // current data is incomplete
                return PBC_ERROR(PBC_error_decompress_failed);
            }
            pbc_memcpy(output_cstring + output_cstring_len, input_cstring + output_buffer_pos,
                       varint_num);
            output_cstring_len += varint_num;
            output_buffer_pos = output_buffer_pos + varint_num;
        }
    }
    output_cstring[output_cstring_len] = 0;
    return output_cstring_len;
}

}  // namespace PBC

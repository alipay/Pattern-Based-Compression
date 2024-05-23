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

#ifndef SRC_COMPRESS_COMPRESS_H_
#define SRC_COMPRESS_COMPRESS_H_

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "hs/hs.h"

static std::vector<int> pattern_len_list;

namespace PBC {

enum CompressTypeFlag {
    COMPRESS_NOT_COMPRESS = 0x1b,
    COMPRESS_PBC_ONLY,
    COMPRESS_SECONDARY_ONLY,
    COMPRESS_PBC_COMBINED
};

enum PBC_ErrorCode {
    PBC_error_no_error = 0,
    PBC_error_compress_failed = 1,
    PBC_error_decompress_failed = 2,
    PBC_error_maxCode = 3
};

#define PBC_ERROR(name) ((size_t)-name)

static __attribute__((unused)) unsigned int PBC_isError(size_t code) {
    return code > (size_t)-PBC_error_maxCode;
}

class PBC_Compress {
public:
    static const size_t DEFAULT_SYMBOL_SIZE;
    static const size_t DEFAULT_BUFFER_SIZE;

public:
    explicit PBC_Compress(size_t symbol_size = DEFAULT_SYMBOL_SIZE,
                          size_t buffer_size = DEFAULT_BUFFER_SIZE);
    virtual ~PBC_Compress();

    // Return pattern nums
    int GetPatternNum() const { return pattern_num_; }

    // HypserScan match_event_handler
    static int OnMatch(unsigned int id, unsigned long long from, unsigned long long to,  // NOLINT
                       unsigned int flags, void* ctx);

    // Read pattern data
    bool ReadData(const char* data, int64_t len);

    // Compress content of buffer input_cstring of size input_cstring_len, into dest buffer
    // output_cstring return size of compressed data, if PBC_isError(return), compression failed
    size_t CompressUsingPattern(const char* input_cstring, size_t input_cstring_len,
                                char* output_cstring);

    // Decompress content of buffer input_cstring of size input_cstring_len, into dest buffer
    // output_cstring return size of decompressed data, if PBC_isError(return), decompression failed
    size_t DecompressUsingPattern(const char* input_cstring, int input_cstring_len,
                                  char* output_cstring);

    size_t CompressUsingPatternWithLength(const char* input_cstring, size_t input_cstring_len,
                                          char* output_cstring);
    size_t DecompressUsingPatternWithLength(const char* input_cstring, int input_cstring_len,
                                            char* output_cstring);

    // int BlockCompressUsingPattern(const char* input_cstring, int input_cstring_len,
    //                          char* output_cstring);
    // int BlockDecompressUsingPattern(const char* input_cstring, int input_cstring_len,
    //                            char* output_cstring);

protected:
    struct patternInfo {
        int num;
        std::vector<int> pos;
        std::string data;
    };

    // Parse hyperscan flag
    static unsigned ParseFlags(const std::string& flagsStr);

    // Whether is special char or not
    static bool IsSpecialChar(char ch);

    // Build hyperscan database
    static hs_database_t* BuildDatabase(const std::vector<const char*>& expressions,
                                        const std::vector<unsigned>& flags,
                                        const std::vector<unsigned>& ids, unsigned int mode);

    // Create hyperscan database
    static bool CreateDatabaseFromFile(const std::vector<std::string>& patterns,
                                       const std::vector<unsigned>& flags,
                                       const std::vector<unsigned>& ids, hs_database_t** db_block);

    // Read and parse patterns
    int64_t ReadPattern(const char* data);

    // Get residual subsequences of the pattern
    int FillingSubsequences(int pattern_id, const std::string& input_string, char* output_cstring,
                            int input_cstring_len) const;

    // Init resource of econdary encoder such as fse, fsst
    virtual void InitSecondaryEncoderResource() = 0;

    // Clear resource of econdary encoder such as fse, fsst
    virtual void CleanSecondaryEncoderResource() = 0;
    virtual void BuildSecondaryEncoder(const char* data, int64_t data_len, int64_t data_pos) = 0;

    // Compress using other secondary encoder such as fse, fsst, return 0 if compress failed.
    virtual size_t ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                          char* output_cstring, int max_output_cstring_len) = 0;

    // Decompress using other secondary encoder such as fse, fsst, return 0 if decompress failed.
    virtual size_t ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                          char* output_cstring, int max_output_cstring_len) = 0;

protected:
    size_t symbol_size_;   // symbol size, default is 256
    size_t buffer_size_;   // buffer size of ouput_buffer_, default is (1024 * 1024)
    int32_t pattern_num_;  // pattern number
    char* output_buffer_;  // stores intermediate results during compression and decompression
    hs_database_t* hs_db_block_ = nullptr;  // hyperscan database
    hs_scratch_t* hs_scratch_ = nullptr;    // hyperscan scratch space

    std::vector<patternInfo> pattern_list_;  // stores pattern infos

    std::vector<std::string> patterns_;  // stores regular expressions
    std::vector<unsigned> flags_;        // stores hyperscan flag
    std::vector<unsigned> ids_;          // stores pattern id
};
}  // namespace PBC

#endif  // SRC_COMPRESS_COMPRESS_H_

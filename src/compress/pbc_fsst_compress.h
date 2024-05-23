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

#ifndef SRC_COMPRESS_PBC_FSST_COMPRESS_H_
#define SRC_COMPRESS_PBC_FSST_COMPRESS_H_

#include <algorithm>
#include <chrono>  // NOLINT
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "compress/compress.h"
#include "deps/fsst/fsst.h"
#include "deps/fsst/libfsst.hpp"

// cereal dependence
#include "cereal/archives/binary.hpp"
#include "cereal/cereal.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/unordered_map.hpp"

namespace PBC {

class PBC_FSST_Compress : public PBC_Compress {
public:
    explicit PBC_FSST_Compress(size_t symbol_size = DEFAULT_SYMBOL_SIZE,
                               size_t buffer_size = DEFAULT_BUFFER_SIZE);
    ~PBC_FSST_Compress();
protected:
    void InitSecondaryEncoderResource() override;
    void CleanSecondaryEncoderResource() override;
    void BuildSecondaryEncoder(const char* data, int64_t data_len, int64_t data_pos) override;
    size_t ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                  char* output_cstring, int max_output_cstring_len) override;
    size_t ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                  char* output_cstring, int max_output_cstring_len) override;

private:
    // fsst objects needed using fsst compress/decompress
    pbc_fsst_encoder_t* pbc_fsst_encoder_ = nullptr;
    pbc_fsst_decoder_t pbc_fsst_decoder_;

public:
    // Serialize fsst encoder
    uint64_t serializeEncoder(pbc_fsst_encoder_t* enc, char** buffer);
    // Deserialize fsst encoder
    pbc_fsst_encoder_t* deserializeEncoder(const char* buffer, int64_t buffer_len);
};
}  // namespace PBC

#endif  // SRC_COMPRESS_PBC_FSST_COMPRESS_H_

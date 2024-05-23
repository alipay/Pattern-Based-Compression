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

#ifndef SRC_COMPRESS_PBC_FSE_COMPRESS_H_
#define SRC_COMPRESS_PBC_FSE_COMPRESS_H_

#include "compress/compress.h"

extern "C" {
#include "deps/fse/fse.h"
#include "deps/fse/hist.h"
}

namespace PBC {

class PBC_FSE_Compress : public PBC_Compress {
public:
    explicit PBC_FSE_Compress(size_t symbol_size = DEFAULT_SYMBOL_SIZE,
                              size_t buffer_size = DEFAULT_BUFFER_SIZE);
    ~PBC_FSE_Compress();

protected:
    void InitSecondaryEncoderResource() override;
    void CleanSecondaryEncoderResource() override;
    void BuildSecondaryEncoder(const char* data, int64_t data_len, int64_t data_pos) override;
    size_t ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                  char* output_cstring, int max_output_cstring_len) override;
    size_t ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                  char* output_cstring, int max_output_cstring_len) override;

private:
    // fse objects needed using fse compress/decompress
    uint32_t fse_maxSymbolValue_;
    uint32_t fse_tableLog_;
    int16_t* fse_normTable_;
    PBC_FSE_CTable* fse_CTable_ = nullptr;
    PBC_FSE_DTable* fse_DTable_ = nullptr;
};
}  // namespace PBC

#endif  // SRC_COMPRESS_PBC_FSE_COMPRESS_H_

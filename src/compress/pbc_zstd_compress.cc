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

#include "compress/pbc_zstd_compress.h"

#include "base/memcpy.h"
#include "common/utils.h"

namespace PBC {

PBC_ZSTD_Compress::PBC_ZSTD_Compress(size_t symbol_size, size_t buffer_size)
    : PBC_Compress(symbol_size, buffer_size) {
    InitSecondaryEncoderResource();
}

PBC_ZSTD_Compress::~PBC_ZSTD_Compress() { CleanSecondaryEncoderResource(); }

void PBC_ZSTD_Compress::InitSecondaryEncoderResource() {}

void PBC_ZSTD_Compress::CleanSecondaryEncoderResource() {
    ZSTD_freeCDict(cdict);
    ZSTD_freeDDict(ddict);
    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);
}

void PBC_ZSTD_Compress::BuildSecondaryEncoder(const char* data, int64_t data_len,
                                              int64_t data_pos) {
    int64_t dict_size = data_len - data_pos;
    char* encoder_buffer = new char[dict_size];
    pbc_memcpy(encoder_buffer, data + data_pos, dict_size);
    cdict = ZSTD_createCDict(encoder_buffer, dict_size, cLevel);
    ddict = ZSTD_createDDict(encoder_buffer, dict_size);
    cctx = ZSTD_createCCtx();
    dctx = ZSTD_createDCtx();
    delete[] encoder_buffer;
}

size_t PBC_ZSTD_Compress::ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    size_t const cSize = ZSTD_compress_usingCDict(cctx, output_cstring, max_output_cstring_len,
                                                  input_cstring, input_cstring_len, cdict);
    if (ZSTD_isError(cSize)) {
        PBC_LOG(ERROR) << "ZSTD_compress_usingCDict failed: " << ZSTD_getErrorName(cSize)
                       << std::endl;
        return 0;
    }
    return cSize;
}

size_t PBC_ZSTD_Compress::ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    size_t const dSize = ZSTD_decompress_usingDDict(dctx, output_cstring, max_output_cstring_len,
                                                    input_cstring, input_cstring_len, ddict);
    if (ZSTD_isError(dSize)) {
        PBC_LOG(ERROR) << "ZSTD_decompress_usingDDict failed: " << ZSTD_getErrorName(dSize)
                       << std::endl;
        return 0;
    }
    return dSize;
}

}  // namespace PBC

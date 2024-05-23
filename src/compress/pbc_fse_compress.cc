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

#include "compress/pbc_fse_compress.h"

#include "base/memcpy.h"

namespace PBC {

PBC_FSE_Compress::PBC_FSE_Compress(size_t symbol_size, size_t buffer_size)
    : PBC_Compress(symbol_size, buffer_size) {
    InitSecondaryEncoderResource();
}

PBC_FSE_Compress::~PBC_FSE_Compress() { CleanSecondaryEncoderResource(); }

void PBC_FSE_Compress::InitSecondaryEncoderResource() {
    fse_normTable_ = new int16_t[symbol_size_ + 1];
}

void PBC_FSE_Compress::CleanSecondaryEncoderResource() {
    delete[] fse_normTable_;
    if (fse_CTable_) PBC_FSE_freeCTable(fse_CTable_);
    if (fse_DTable_) PBC_FSE_freeDTable(fse_DTable_);
}

void PBC_FSE_Compress::BuildSecondaryEncoder(const char* data, int64_t data_len, int64_t data_pos) {
    unsigned maxSymbolValue = symbol_size_;
    unsigned tableLog;

    PBC_FSE_readNCount(fse_normTable_, &maxSymbolValue, &tableLog, data + data_pos,
                       data_len - data_pos);
    fse_maxSymbolValue_ = maxSymbolValue;
    fse_tableLog_ = tableLog;

    fse_CTable_ = PBC_FSE_createCTable(fse_maxSymbolValue_, fse_tableLog_);
    PBC_FSE_buildCTable(fse_CTable_, fse_normTable_, fse_maxSymbolValue_, fse_tableLog_);
    fse_DTable_ = PBC_FSE_createDTable(fse_tableLog_);
    PBC_FSE_buildDTable(fse_DTable_, fse_normTable_, fse_maxSymbolValue_, fse_tableLog_);
}

size_t PBC_FSE_Compress::ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                                char* output_cstring, int max_output_cstring_len) {
    return PBC_FSE_compress_usingCTable(output_cstring, max_output_cstring_len, input_cstring,
                                        input_cstring_len, fse_CTable_);
}

size_t PBC_FSE_Compress::ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                                char* output_cstring, int max_output_cstring_len) {
    return PBC_FSE_decompress_usingDTable(output_cstring, max_output_cstring_len, input_cstring,
                                          input_cstring_len, fse_DTable_);
}

}  // namespace PBC

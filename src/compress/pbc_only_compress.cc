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

#include "compress/pbc_only_compress.h"

#include "base/memcpy.h"

namespace PBC {

PBC_ONLY_Compress::PBC_ONLY_Compress(size_t symbol_size, size_t buffer_size)
    : PBC_Compress(symbol_size, buffer_size) {
    InitSecondaryEncoderResource();
}

PBC_ONLY_Compress::~PBC_ONLY_Compress() { CleanSecondaryEncoderResource(); }

void PBC_ONLY_Compress::InitSecondaryEncoderResource() {}

void PBC_ONLY_Compress::CleanSecondaryEncoderResource() {}

void PBC_ONLY_Compress::BuildSecondaryEncoder(const char* data, int64_t data_len,
                                              int64_t data_pos) {}

size_t PBC_ONLY_Compress::ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    return 0;
}

size_t PBC_ONLY_Compress::ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    return 0;
}

}  // namespace PBC

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

#include "compress-c.h"  // NOLINT

#include "compress/compress_factory.h"
#include "train/pbc_train.h"

using PBC::PBC_Compress;
using PBC::PBC_Train;

void* PBC_createCompressCtx(CompressMethod compress_method) {
    return PBC::CompressFactory::CreatePBCCompress(PBC::CompressMethod(compress_method));
}

unsigned int PBC_setPattern(void* pbc_ctx, char* pattern_buffer, size_t pattern_buffer_len) {
    PBC_Compress* pbc = reinterpret_cast<PBC_Compress*>(pbc_ctx);
    return pbc->ReadData(pattern_buffer, pattern_buffer_len);
}

size_t PBC_compressUsingPattern(void* pbc_ctx, char* data, size_t data_len, char* compress_buffer) {
    PBC_Compress* pbc = reinterpret_cast<PBC_Compress*>(pbc_ctx);
    return pbc->CompressUsingPattern(data, data_len, compress_buffer);
}

size_t PBC_decompressUsingPattern(void* pbc_ctx, char* compress_data, size_t compress_data_len,
                                  char* data_buffer) {
    PBC_Compress* pbc = reinterpret_cast<PBC_Compress*>(pbc_ctx);
    return pbc->DecompressUsingPattern(compress_data, compress_data_len, data_buffer);
}

int PBC_getCtxPatternNum(const void* pbc_ctx) {
    const PBC_Compress* pbc = reinterpret_cast<const PBC_Compress*>(pbc_ctx);
    return pbc->GetPatternNum();
}

void* PBC_createTrainCtx(int compress_method, int thread_num) {
    return new PBC_Train(PBC::CompressMethod(compress_method), thread_num);
}

void PBC_loadPbcTrainData(void* pbc_ctx, char* file_buffer_train, size_t file_buffer_len,
                          int data_type) {
    PBC_Train* pbc = reinterpret_cast<PBC_Train*>(pbc_ctx);
    pbc->LoadData(file_buffer_train, file_buffer_len, data_type);
}

size_t PBC_trainPattern(void* pbc_ctx, int pattern_size, char** pattern_buffer) {
    PBC_Train* pbc = reinterpret_cast<PBC_Train*>(pbc_ctx);
    return pbc->TrainPattern(pattern_size, pattern_buffer);
}

unsigned int PBC_isError(size_t code) { return PBC::PBC_isError(code); }

void PBC_freeTrainCtx(void* pbc_ctx) {
    if (pbc_ctx == NULL) {
        return;
    }
    PBC_Train* pbc_compress = reinterpret_cast<PBC_Train*>(pbc_ctx);
    delete pbc_compress;
}

void PBC_freePBCDict(void* pbc_ctx) {
    if (pbc_ctx == NULL) {
        return;
    }
    PBC_Compress* pbc_compress = reinterpret_cast<PBC_Compress*>(pbc_ctx);
    delete pbc_compress;
}

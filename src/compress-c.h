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

#ifndef SRC_COMPRESS_C_H_
#define SRC_COMPRESS_C_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TYPE_VARCHAR 0
#define TYPE_RECORD 1

typedef enum { PBC_ONLY, PBC_FSE, PBC_FSST, PBC_ZSTD } CompressMethod;

// Create pbc compress object
void* PBC_createCompressCtx(CompressMethod compress_method);

// Set pattern
unsigned int PBC_setPattern(void* pbc_ctx, char* pattern, size_t pattern_buffer_len);

// PBC compress
size_t PBC_compressUsingPattern(void* pbc_ctx, char* data, size_t data_len, char* compress_buffer);

// PBC decompress
size_t PBC_decompressUsingPattern(void* pbc_ctx, char* compress_data, size_t compress_data_len,
                                  char* data_buffer);

// Get pattern number
int PBC_getCtxPatternNum(const void* pbc_ctx);

// Create pbc train object
void* PBC_createTrainCtx(int compress_method, int thread_num);

// Load pbc train data
void PBC_loadPbcTrainData(void* pbc_ctx, char* data_buffer, size_t len, int data_type);

// Train pattern
size_t PBC_trainPattern(void* pbc_ctx, int k, char** pattern_buffer);

// Whether is error or not
unsigned int PBC_isError(size_t code);

// Free pbc train object
void PBC_freeTrainCtx(void* pbc_ctx);

// Free pbc compress object
void PBC_freePBCDict(void* pbc_ctx);

#ifdef __cplusplus
}
#endif
#endif  // SRC_COMPRESS_C_H_

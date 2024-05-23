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

extern "C" {
#include <fcntl.h>
#include <pbc/compress-c.h>  // presumes pbc library is installed
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
}

#include <iostream>

// Read data from file
static int64_t ReadFile(const char* file_path, char** data_buffer) {
    struct stat statbuf;
    if (stat(file_path, &statbuf) != 0 || statbuf.st_size <= 0) {
        std::cerr << "The input file does not exist or is empty: " << file_path << std::endl;
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    char* temp =
        reinterpret_cast<char*>(mmap(NULL, statbuf.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0));

    *data_buffer = new char[statbuf.st_size + 1];

    memcpy(*data_buffer, temp, statbuf.st_size);
    return statbuf.st_size;
}

int main(int argc, const char** argv) {
    const char* const exeName = argv[0];
    if (argc != 3) {
        printf("wrong arguments\n");
        printf("usage:\n%s compressFile patternFile\n", exeName);
        return -1;
    }

    const char* const compressFile = argv[1];
    const char* const patternFile = argv[2];
    // set compress method (PBC_ONLY, PBC_FSE, PBC_FSST, PBC_ZSTD)
    CompressMethod compress_method = CompressMethod::PBC_FSST;
    // set read date type (TYPE_RECORD, TYPE_VARCHAR)
    int data_type = TYPE_RECORD;
    char* data_buffer = nullptr;
    char* pattern_buffer = nullptr;

    // Read compress file
    size_t data_buffer_len = ReadFile(compressFile, &data_buffer);
    // Read pattern file
    size_t pattern_buffer_len = ReadFile(patternFile, &pattern_buffer);
    // Create pbc compress object
    void* pbc_compress = PBC_createCompressCtx(compress_method);
    // Load pattern data
    PBC_setPattern(pbc_compress, pattern_buffer, pattern_buffer_len);

    char* compress_buffer = new char[data_buffer_len];
    char* decompress_buffer = new char[data_buffer_len];
    size_t data_pos = 0;
    size_t total_compressed_len = 0;
    while (data_pos < data_buffer_len) {
        int data_len = 0;
        // Split data by '\n'
        while (data_pos + data_len < data_buffer_len && data_buffer[data_pos + data_len] != '\n') {
            data_len++;
        }
        // Compress data
        size_t compress_len = PBC_compressUsingPattern(pbc_compress, data_buffer + data_pos,
                                                       data_len, compress_buffer);
        if (PBC_isError(compress_len)) {
            std::cerr << "PBC_compressUsingPattern error!" << std::endl;
            return -1;
        }
        total_compressed_len += compress_len;
        // Decompress data
        size_t decompress_len = PBC_decompressUsingPattern(pbc_compress, compress_buffer,
                                                           compress_len, decompress_buffer);
        if (PBC_isError(decompress_len)) {
            std::cerr << "PBC_decompressUsingPattern error!" << std::endl;
            return -1;
        }
        // Check data
        if (data_len != decompress_len ||
            memcmp(data_buffer + data_pos, decompress_buffer, data_len) != 0) {
            std::cerr << "PBC compress/decompress error!" << std::endl;
            return -1;
        }
        data_pos += data_len + 1;
    }
    std::cout << "compress rate:"
              << static_cast<double>(total_compressed_len) / static_cast<double>(data_buffer_len)
              << std::endl;
    std::cout << "original size:" << data_buffer_len
              << " -> compressed size: " << total_compressed_len << std::endl;
    delete[] data_buffer;
    delete[] pattern_buffer;
    delete[] compress_buffer;
    delete[] decompress_buffer;
    PBC_freePBCDict(pbc_compress);
    return 0;
}

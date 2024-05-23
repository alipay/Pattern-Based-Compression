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
static size_t ReadFile(const char* file_path, char** data_buffer) {
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

static size_t SampleData(const char* data_buffer, size_t data_buffer_len, char* train_data_buffer,
                         size_t train_num) {
    size_t data_pos = 0;
    size_t record_num = 0;
    while (data_pos < data_buffer_len) {
        while (data_pos < data_buffer_len && data_buffer[data_pos] != '\n') {
            data_pos++;
        }
        data_pos++;
        record_num++;
    }
    data_pos = 0;
    size_t sample_step = record_num / train_num;
    size_t i = 0;
    size_t train_buffer_pos = 0;
    while (data_pos < data_buffer_len) {
        size_t record_len = 0;
        while (data_pos + record_len < data_buffer_len &&
               data_buffer[data_pos + record_len] != '\n') {
            record_len++;
        }
        i++;
        if (i % sample_step == 0) {
            // copy train data
            memcpy(train_data_buffer + train_buffer_pos, data_buffer + data_pos, record_len);
            train_buffer_pos += record_len;
            memcpy(train_data_buffer + train_buffer_pos, "\n", 1);
            train_buffer_pos += 1;
        }
        data_pos += record_len + 1;
    }
    return train_buffer_pos;
}

int main(int argc, const char** argv) {
    const char* const exeName = argv[0];
    if (argc != 3) {
        printf("wrong arguments\n");
        printf("usage:\n%s inputFile outputFile\n", exeName);
        return 1;
    }

    const char* const inputFileName = argv[1];
    const char* const outputFileName = argv[2];
    // set compress method (PBC_ONLY, PBC_FSE, PBC_FSST, PBC_ZSTD)
    int compress_method = CompressMethod::PBC_FSST;
    // set train thread num
    int thread_num = 64;
    // set train record number
    int train_num = 1000;
    // set target pattern number
    int pattern_num = 100;
    // set read date type (TYPE_RECORD, TYPE_VARCHAR)
    int data_type = TYPE_RECORD;
    char* data_buffer = nullptr;
    char* pattern_buffer = nullptr;
    // Read data from file
    size_t data_buffer_len = ReadFile(inputFileName, &data_buffer);

    char* train_data_buffer = new char[data_buffer_len];
    // Sample 1000 training data from original data
    size_t train_data_buffer_len =
        SampleData(data_buffer, data_buffer_len, train_data_buffer, train_num);

    // Create pbc train object
    void* pbc_ctx = PBC_createTrainCtx(compress_method, thread_num);
    // Load traning data
    PBC_loadPbcTrainData(pbc_ctx, train_data_buffer, train_data_buffer_len, data_type);
    // Training pattern
    size_t pattern_buffer_len = PBC_trainPattern(pbc_ctx, pattern_num, &pattern_buffer);
    int fd = open(outputFileName, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        return 1;
    }
    // Write pattern data to file
    write(fd, pattern_buffer, pattern_buffer_len);
    close(fd);
    delete[] data_buffer;
    delete[] pattern_buffer;
    PBC_freeTrainCtx(pbc_ctx);
    return 0;
}

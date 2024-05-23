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

#include <fcntl.h>
#include <unistd.h>

#include <chrono>  // NOLINT
#include <ctime>
#include <iostream>

#include "base/memcpy.h"
#include "common/utils.h"
#include "compress/compress_factory.h"
#include "train/pbc_train.h"

using PBC::ERROR;
using PBC::INFO;

constexpr int MAX_PATTERN_SIZE = (1024 * 1024 * 8);
constexpr int DEFAULT_MAX_DECOMPRESS_SIZE = (1024 * 1024);
constexpr int DEFAULT_PATTERN_SIZE = 20;
constexpr int DEFAULT_TRAIN_DATA_SIZE = 500;
constexpr int DEFAULT_TRAIN_THREAD_NUM = 16;

enum PBCOperation { NO_OPERATION, TRAIN_PATTERN, TEST_COMPRESS, COMPRESS, DECOMPRESS };

static struct config {
    PBCOperation operation = PBCOperation::NO_OPERATION;
    int32_t target_pattern_size = DEFAULT_PATTERN_SIZE;
    int train_data_number = DEFAULT_TRAIN_DATA_SIZE;
    char* inputfile_path = nullptr;
    char* patternfile_path = nullptr;
    char* outputfile_path = nullptr;
    int32_t train_thread_num = DEFAULT_TRAIN_THREAD_NUM;
    int32_t input_type = TYPE_RECORD;
    PBC::CompressMethod compress_method = PBC::CompressMethod::PBC_ONLY;
    int log_level = 1;  // 0 print all logs, 1 print info logs, 2 print error log, 3 print error
                        // logs, >=4 print no log
    int use_default_log_level = 1;
} config;

static void usage();

// Parse command line
static bool ParseOptions(int argc, const char** argv) {
    for (int i = 1; i < argc; i++) {
        int lastarg = (i == argc - 1);

        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
        } else if (!strcmp(argv[i], "--train-pattern")) {
            config.operation = PBCOperation::TRAIN_PATTERN;
        } else if (!strcmp(argv[i], "--test-compress")) {
            config.operation = PBCOperation::TEST_COMPRESS;
        } else if ((!strcmp(argv[i], "--compress") || !strcmp(argv[i], "-c"))) {
            config.operation = PBCOperation::COMPRESS;
        } else if ((!strcmp(argv[i], "--decompress") || !strcmp(argv[i], "-d"))) {
            config.operation = PBCOperation::DECOMPRESS;
        } else if ((!strcmp(argv[i], "--inputfile") || !strcmp(argv[i], "-i")) && !lastarg) {
            config.inputfile_path = const_cast<char*>(argv[++i]);
        } else if ((!strcmp(argv[i], "--pattern-path") || !strcmp(argv[i], "-p")) && !lastarg) {
            config.patternfile_path = const_cast<char*>(argv[++i]);
        } else if ((!strcmp(argv[i], "--outputfile") || !strcmp(argv[i], "-o")) && !lastarg) {
            config.outputfile_path = const_cast<char*>(argv[++i]);
        } else if (!strcmp(argv[i], "--compress-method")) {
            int next_pos = ++i;
            if (!strcasecmp(argv[next_pos], "pbc_only")) {
                config.compress_method = PBC::CompressMethod::PBC_ONLY;
            } else if (!strcasecmp(argv[next_pos], "pbc_fsst")) {
                config.compress_method = PBC::CompressMethod::PBC_FSST;
            } else if (!strcasecmp(argv[next_pos], "pbc_fse")) {
                config.compress_method = PBC::CompressMethod::PBC_FSE;
            } else if (!strcasecmp(argv[next_pos], "pbc_zstd")) {
                config.compress_method = PBC::CompressMethod::PBC_ZSTD;
            }
        } else if (!strcmp(argv[i], "--pattern-size") && !lastarg) {
            config.target_pattern_size = atoi(argv[++i]);
            if (config.target_pattern_size > MAX_PATTERN_SIZE) {
                std::cerr << "dict size overflow" << std::endl;
                return false;
            }
        } else if (!strcmp(argv[i], "--train-data-number") && !lastarg) {
            config.train_data_number = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--train-thread-num") && !lastarg) {
            config.train_thread_num = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--varchar")) {
            config.input_type = TYPE_VARCHAR;
        } else if (!strcmp(argv[i], "--log-level") && !lastarg) {
            config.log_level = atoi(argv[++i]);
            config.use_default_log_level = 0;
        }
    }
    return true;
}

static void usage() {
    std::cerr <<
        "\n"
           "Usage: pbc [OPTIONS] [arg [arg ...]]\n"
           "  --help             Output this help and exit.\n"
           "  --train-pattern -i <inputFile> -p <patternFile> [--compress-method <pbc_only/pbc_fse/pbc_fsst/pbc_zstd>] [--pattern-size <pattern_size>] [--train-data-number <train_data_number>] [--train-thread-num <train_thread_num>] [--varchar].\n"
           "  --test-compress -i <inputFile> -p <patternFile> [--compress-method <pbc_only/pbc_fse/pbc_fsst/pbc_zstd>] [--varchar].\n"
           "  -c/--compress -i <inputFile> -p <patternFile> [-o <outputFile>].\n"
           "  -d/--decompress -i <inputFile> -p <patternFile> [-o <outputFile>].\n"
           "  -i <inputFile>           Input File, train-pattern/test-compress(not default), compress/decompress(default: stdin).\n"
           "  -p <patternFile>         Pattern File, not default.\n"
           "  -o <outputFile>          Output File, only effected when compress/decompress, default is stdout.\n"
           "  --compress-method        Compress method, one of pbc_only, pbc_fse, pbc_fsst, pbc_zstd, default is pbc_only.\n"
           "  --pattern-size           The number of expected generate, default is 20.\n"
           "  --train-data-number      The number of data used for training pattern, default is 500.\n"
           "  --train-thread-num       The thread num used for training pattern, default is 16.\n"
           "  --varchar                Data type of input file, only effected when train-pattern and test-compress, default is Record(split by \'\\n\').\n"
           "\n"
           "Examples:\n"
           "  pbc --train-pattern -i inputFile -p patternFile --compress-method pbc_fse --pattern-size 50 --train-data-number 1000 --train-thread-num 64 --varchar\n"
           "  pbc --test-compress -i inputFile -p patternFile --compress-method pbc_fse --varchar\n"
           "  pbc --compress -i inputFile -p patternFile -o outputFile\n"
           "  cat inputFile | pbc --compress -p patternFile > outputFile\n"
           "  pbc --decompress -i inputFile -p patternFile -o outputFile\n"
           "  cat inputFile | pbc --decompress -p patternFile > outputFile\n"
        << std::endl;
    exit(0);
}

static std::string CompressMethodToString(PBC::CompressMethod compress_method) {
    switch (compress_method) {
        case PBC::CompressMethod::PBC_ONLY:
            return "PBC_ONLY";
        case PBC::CompressMethod::PBC_FSE:
            return "PBC_FSE";
        case PBC::CompressMethod::PBC_FSST:
            return "PBC_FSST";
        case PBC::CompressMethod::PBC_ZSTD:
            return "PBC_ZSTD";
    }
    return "UNKONW_COMPRESS_METHOD";
}

static int PBCTrainPattern() {
    PBC::SetPBCLogLevel(config.log_level);
    PBC_LOG(INFO) << "operation: train_pattern" << std::endl;
    PBC_LOG(INFO) << "compress method: " << CompressMethodToString(config.compress_method)
                  << std::endl;
    PBC_LOG(INFO) << "compress file path:"
                  << (config.inputfile_path == nullptr ? "NULL" : config.inputfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "pattern file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "train_data_number: " << config.train_data_number << std::endl;
    PBC_LOG(INFO) << "target pattern size: " << config.target_pattern_size << std::endl;

    char* original_buffer = nullptr;
    char* records_buffer = nullptr;
    char* train_buffer = nullptr;
    char* pattern_buffer = nullptr;
    int64_t original_len = 0;
    int64_t records_buffer_len = 0;
    int64_t train_buffer_len = 0;
    int64_t pattern_buffer_len = 0;
    int64_t record_num = 0;
    int32_t max_record_len = 0;

    original_len = PBC::ReadFile(config.inputfile_path, &original_buffer);
    if (original_len < 0) {
        return -1;
    }
    records_buffer_len = PBC::ReadDataFromBuffer(config.input_type, original_buffer, original_len,
                                                 &records_buffer, record_num, max_record_len);
    train_buffer_len = PBC::SamplingFromData(records_buffer, records_buffer_len, record_num,
                                             &train_buffer, config.train_data_number);

    auto start_train_time = std::chrono::steady_clock::now();
    PBC::PBC_Train* pbc_train = new PBC::PBC_Train(config.compress_method, config.train_thread_num);
    pbc_train->PBC::PBC_Train::LoadData(train_buffer, train_buffer_len, /*data_type=*/TYPE_VARCHAR);
    pattern_buffer_len =
        pbc_train->PBC::PBC_Train::TrainPattern(config.target_pattern_size, &pattern_buffer);
    auto end_train_time = std::chrono::steady_clock::now();
    PBC_LOG(INFO) << "train pattern cost time: "
                  << std::chrono::duration<double>(end_train_time - start_train_time).count() << "s"
                  << std::endl;

    PBC::WriteFile(config.patternfile_path, pattern_buffer, pattern_buffer_len);
    delete pbc_train;
    delete[] records_buffer;
    delete[] train_buffer;
    delete[] original_buffer;
    delete[] pattern_buffer;
    return 0;
}

static int PBCTestCompress() {
    PBC::SetPBCLogLevel(config.log_level);
    PBC_LOG(INFO) << "operation: test_compress" << std::endl;
    PBC_LOG(INFO) << "compress method: " << CompressMethodToString(config.compress_method)
                  << std::endl;
    PBC_LOG(INFO) << "compress file path:"
                  << (config.inputfile_path == nullptr ? "NULL" : config.inputfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "pattern file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;

    PBC::PBC_Compress* pbc_compress =
        PBC::CompressFactory::CreatePBCCompress(config.compress_method);
    int64_t pattern_buffer_len = 0, test_buffer_len = 0, input_buffer_len = 0;
    int64_t record_num = 0;
    int32_t max_record_len = 0;
    char* pattern_buffer = nullptr;
    char* test_buffer = nullptr;
    char* input_buffer = nullptr;

    pattern_buffer_len = PBC::ReadFile(config.patternfile_path, &pattern_buffer);
    if (!pbc_compress->ReadData(pattern_buffer, pattern_buffer_len)) {
        PBC_LOG(ERROR) << "read pattern failed." << std::endl;
        return -1;
    }

    input_buffer_len = PBC::ReadFile(config.inputfile_path, &input_buffer);
    test_buffer_len = PBC::ReadDataFromBuffer(config.input_type, input_buffer, input_buffer_len,
                                              &test_buffer, record_num, max_record_len);
    PBC_LOG(INFO) << "Toal test record number: " << record_num << std::endl;
    PBC_LOG(INFO) << "max_record_len: " << max_record_len << std::endl;

    double compress_time = 0.0, decompress_time = 0.0;
    char* compressed_data = new char[max_record_len * 2];
    char* decompressed_data = new char[DEFAULT_MAX_DECOMPRESS_SIZE];
    char* record = new char[max_record_len + 1];
    int64_t buffer_ptr = 0;
    int total_compressed_len = 0, raw_len = 0;
    int compress_pbc_only = 0, compress_secondary_only = 0, compress_pbc_combined = 0,
        compress_failed = 0;

    while (buffer_ptr < test_buffer_len) {
        int32_t record_len = 0;
        pbc_memcpy(&record_len, test_buffer + buffer_ptr, sizeof(int32_t));
        buffer_ptr += sizeof(int32_t);

        pbc_memcpy(record, test_buffer + buffer_ptr, record_len);
        buffer_ptr += record_len;
        // record[record_len] = 0;
        if (record_len == 0) {
            continue;
        }
        raw_len += record_len;
        auto compress_start_time = std::chrono::steady_clock::now();
        size_t compressed_size =
            pbc_compress->CompressUsingPattern(record, record_len, compressed_data);

        auto compress_stop_time = std::chrono::steady_clock::now();
        compress_time +=
            std::chrono::duration<double>(compress_stop_time - compress_start_time).count();

        if (PBC::PBC_isError(compressed_size)) {
            return -1;
        } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_NOT_COMPRESS) {
            compress_failed++;
            total_compressed_len += record_len;
        } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_ONLY) {
            compress_pbc_only++;
            total_compressed_len += compressed_size;
        } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_SECONDARY_ONLY) {
            compress_secondary_only++;
            total_compressed_len += compressed_size;
        } else if (compressed_data[0] == PBC::CompressTypeFlag::COMPRESS_PBC_COMBINED) {
            compress_pbc_combined++;
            total_compressed_len += compressed_size;
        }

        compressed_data[compressed_size] = 0;
        auto decompress_start_time = std::chrono::steady_clock::now();
        size_t decompressed_size = pbc_compress->DecompressUsingPattern(
            compressed_data, compressed_size, decompressed_data);
        auto decompress_stop_time = std::chrono::steady_clock::now();

        decompress_time +=
            std::chrono::duration<double>(decompress_stop_time - decompress_start_time).count();
        if (PBC::PBC_isError(decompressed_size)) {
            PBC_LOG(ERROR) << "decompress failed." << std::endl;
            return -1;
        } else if (decompressed_size != record_len ||
                   memcmp(decompressed_data, record, record_len) != 0) {
            PBC_LOG(ERROR) << "compress or decompress error" << std::endl;
            PBC_LOG(ERROR) << "record_len=" << record_len << ", compressed_size=" << compressed_size
                           << ", decompressed_size=" << decompressed_size << std::endl;
            PBC_LOG(ERROR) << "compressed data:" << compressed_data << std::endl;
            PBC_LOG(ERROR) << "decompressed data:" << decompressed_data << std::endl;
            PBC_LOG(ERROR) << "original_key:" << record << std::endl;
        }
        memset(record, 0, max_record_len);
    }

    PBC_LOG(INFO) << "compression rate:"
                  << static_cast<double>(total_compressed_len) / static_cast<double>(raw_len)
                  << std::endl;
    PBC_LOG(INFO) << "compression :" << raw_len << " -> " << total_compressed_len << std::endl;
    PBC_LOG(INFO) << "compression speed: "
                  << static_cast<double>(raw_len) / static_cast<double>(1024) /
                         static_cast<double>(1024) / compress_time
                  << "MB/s" << std::endl;
    PBC_LOG(INFO) << "decompression speed: "
                  << static_cast<double>(raw_len) / static_cast<double>(1024) /
                         static_cast<double>(1024) / decompress_time
                  << "MB/s" << std::endl;
    PBC_LOG(INFO) << "compress_pbc_only rate : "
                  << compress_pbc_only / static_cast<double>(record_num) << std::endl;
    PBC_LOG(INFO) << "compress_secondary_only rate : "
                  << compress_secondary_only / static_cast<double>(record_num) << std::endl;
    PBC_LOG(INFO) << "compress_pbc_combined rate : "
                  << compress_pbc_combined / static_cast<double>(record_num) << std::endl;
    PBC_LOG(INFO) << "compress_failed rate : " << compress_failed / static_cast<double>(record_num)
                  << std::endl;
    delete[] compressed_data;
    delete[] decompressed_data;
    delete[] record;
    delete[] input_buffer;
    delete[] pattern_buffer;
    delete[] test_buffer;
    delete pbc_compress;
    return 0;
}

static int PBCCompressFile() {
    // file compress set default log level as ERROR
    if (config.use_default_log_level) {
        PBC::SetPBCLogLevel(PBC::PBCLogLevel::ERROR);
    } else {
        PBC::SetPBCLogLevel(config.log_level);
    }

    PBC_LOG(INFO) << "operation: compress" << std::endl;
    PBC_LOG(INFO) << "compress method: " << CompressMethodToString(config.compress_method)
                  << std::endl;
    PBC_LOG(INFO) << "compress file path:"
                  << (config.inputfile_path == nullptr ? "NULL" : config.inputfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "pattern file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "output file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;

    auto compress_start_time = std::chrono::steady_clock::now();
    int input_fd = STDIN_FILENO;  // use stdout as default output
    if (config.inputfile_path) {
        input_fd = open(config.inputfile_path, O_RDONLY);
        PBC_LOG(INFO) << "open file:" << config.inputfile_path << std::endl;
    } else {
        PBC_LOG(INFO) << "read data from stdin" << std::endl;
    }
    if (input_fd == -1) {
        PBC_LOG(ERROR) << "can't open file: " << config.inputfile_path << std::endl;
        return -1;
    }

    int output_fd = STDOUT_FILENO;  // use stdout as default output
    if (config.outputfile_path) {
        output_fd = open(config.outputfile_path, O_WRONLY | O_TRUNC | O_CREAT);
        PBC_LOG(INFO) << "open or create file:" << config.outputfile_path << std::endl;
    }
    if (output_fd == -1) {
        PBC_LOG(ERROR) << "can't open file: " << config.outputfile_path << std::endl;
        return -1;
    }

    PBC::PBC_Compress* pbc_compress =
        PBC::CompressFactory::CreatePBCCompress(config.compress_method);
    char* pattern_buffer = nullptr;
    int64_t pattern_buffer_len = PBC::ReadFile(config.patternfile_path, &pattern_buffer);
    if (!pbc_compress->ReadData(pattern_buffer, pattern_buffer_len)) {
        PBC_LOG(ERROR) << "read pattern failed." << std::endl;
        return -1;
    }

    std::string input;
    size_t compressed_buffer_len = 10 * 1024;
    char* compressed_buffer = new char[compressed_buffer_len];
    int count = 0;
    size_t read_buffer_len = 10 * 1024;
    char* read_buffer = new char[read_buffer_len];
    std::string last_read;
    ssize_t bytes_read;

    while ((bytes_read = read(input_fd, read_buffer, read_buffer_len)) > 0) {
        int start_pos = 0;
        for (int i = 0; i < bytes_read; i++) {
            if (read_buffer[i] == '\n') {
                if (i == start_pos && last_read.empty()) {
                    write(output_fd, "\n", 1);
                    start_pos = i + 1;
                    continue;
                }

                size_t original_len = last_read.length() + (i - start_pos);
                if (original_len > DEFAULT_MAX_DECOMPRESS_SIZE) {
                    PBC_LOG(ERROR) << "compress failed: single record(" << original_len
                                   << " bytes) > max compress size(" << DEFAULT_MAX_DECOMPRESS_SIZE
                                   << " bytes).";
                    return -1;
                }
                if (compressed_buffer_len < original_len * 2) {
                    delete[] compressed_buffer;
                    compressed_buffer_len = (original_len * 2 / 1024 + 1) * 1024;
                    compressed_buffer = new char[compressed_buffer_len];
                }

                size_t compressed_size;
                if (last_read.empty()) {
                    compressed_size = pbc_compress->CompressUsingPatternWithLength(
                        read_buffer + start_pos, i - start_pos, compressed_buffer);
                } else {
                    last_read.append(read_buffer + start_pos, i - start_pos);
                    compressed_size = pbc_compress->CompressUsingPatternWithLength(
                        last_read.c_str(), last_read.length(), compressed_buffer);
                    last_read.clear();
                }
                count++;

                if (!PBC::PBC_isError(compressed_size)) {
                    write(output_fd, compressed_buffer, compressed_size);
                    write(output_fd, "\n", 1);
                } else {
                    PBC_LOG(ERROR) << "compress failed" << std::endl;
                    return -1;
                }
                start_pos = i + 1;
            }
        }
        if (start_pos != bytes_read) {
            last_read.append(read_buffer + start_pos, bytes_read - start_pos);
        }
    }

    if (bytes_read == -1) {
        PBC_LOG(ERROR) << "read failed" << std::endl;
        return -1;
    }

    if (!last_read.empty()) {
        if (last_read.length() > DEFAULT_MAX_DECOMPRESS_SIZE) {
            PBC_LOG(ERROR) << "compress failed: single record(" << last_read.length()
                           << " bytes) > max compress size(" << DEFAULT_MAX_DECOMPRESS_SIZE
                           << " bytes).";
            return -1;
        }
        if (compressed_buffer_len < last_read.length() * 2) {
            delete[] compressed_buffer;
            compressed_buffer_len = last_read.length() * 2;
            compressed_buffer = new char[compressed_buffer_len];
        }
        size_t compressed_size = pbc_compress->CompressUsingPatternWithLength(
            last_read.c_str(), last_read.length(), compressed_buffer);
        if (PBC::PBC_isError(compressed_size)) {
            PBC_LOG(ERROR) << "compress failed" << std::endl;
            return -1;
        }
        write(output_fd, compressed_buffer, compressed_size);
        count++;
    }
    PBC_LOG(INFO) << "data count = " << count << std::endl;

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }
    delete[] pattern_buffer;
    delete[] compressed_buffer;
    delete[] read_buffer;
    delete pbc_compress;
    auto compress_stop_time = std::chrono::steady_clock::now();
    PBC_LOG(INFO) << "compress file cost time: "
                  << std::chrono::duration<double>(compress_stop_time - compress_start_time).count()
                  << "s" << std::endl;
    return 0;
}

static int PBCFileDecompressFile() {
    // file compress set default log level as ERROR
    if (config.use_default_log_level) {
        PBC::SetPBCLogLevel(PBC::PBCLogLevel::ERROR);
    } else {
        PBC::SetPBCLogLevel(config.log_level);
    }

    PBC_LOG(INFO) << "operation: decompress" << std::endl;
    PBC_LOG(INFO) << "compress method: " << CompressMethodToString(config.compress_method)
                  << std::endl;
    PBC_LOG(INFO) << "decompress file path:"
                  << (config.inputfile_path == nullptr ? "NULL" : config.inputfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "pattern file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;
    PBC_LOG(INFO) << "output file path:"
                  << (config.patternfile_path == nullptr ? "NULL" : config.patternfile_path)
                  << std::endl;

    auto decompress_start_time = std::chrono::steady_clock::now();
    PBC_LOG(INFO) << "decompress" << std::endl;

    int input_fd = STDIN_FILENO;  // use stdout as default output
    if (config.inputfile_path) {
        input_fd = open(config.inputfile_path, O_RDONLY);
        PBC_LOG(INFO) << "open file:" << config.inputfile_path << std::endl;
    } else {
        PBC_LOG(INFO) << "read data from stdin" << std::endl;
    }
    if (input_fd == -1) {
        PBC_LOG(ERROR) << "can't open file: " << config.inputfile_path << std::endl;
        return -1;
    }

    int output_fd = STDOUT_FILENO;  // use stdout as default output
    if (config.outputfile_path) {
        output_fd = open(config.outputfile_path, O_WRONLY | O_TRUNC | O_CREAT);
        PBC_LOG(INFO) << "open or create file:" << config.outputfile_path << std::endl;
    }
    if (output_fd == -1) {
        PBC_LOG(ERROR) << "can't open file: " << config.outputfile_path << std::endl;
        return -1;
    }

    PBC::PBC_Compress* pbc_compress =
        PBC::CompressFactory::CreatePBCCompress(config.compress_method);
    char* pattern_buffer = nullptr;
    int64_t pattern_buffer_len = PBC::ReadFile(config.patternfile_path, &pattern_buffer);
    if (!pbc_compress->ReadData(pattern_buffer, pattern_buffer_len)) {
        PBC_LOG(ERROR) << "read pattern failed." << std::endl;
        return -1;
    }

    size_t decompressed_buffer_len = DEFAULT_MAX_DECOMPRESS_SIZE;
    char* decompressed_buffer = new char[decompressed_buffer_len];
    int count = 0;
    size_t read_buffer_len = 10 * 1024;
    char* read_buffer = new char[read_buffer_len];
    std::string last_read;
    ssize_t bytes_read;

    while ((bytes_read = read(input_fd, read_buffer, read_buffer_len)) > 0) {
        int start_pos = 0;
        for (int i = 0; i < bytes_read; i++) {
            if (read_buffer[i] == '\n') {
                if (i == start_pos && last_read.empty()) {
                    write(output_fd, "\n", 1);
                    start_pos = i + 1;
                    continue;
                }

                size_t decompressed_size;
                if (last_read.empty()) {
                    decompressed_size = pbc_compress->DecompressUsingPatternWithLength(
                        read_buffer + start_pos, i - start_pos, decompressed_buffer);
                } else {
                    last_read.append(read_buffer + start_pos, i - start_pos);
                    decompressed_size = pbc_compress->DecompressUsingPatternWithLength(
                        last_read.c_str(), last_read.length(), decompressed_buffer);
                }
                count++;

                if (!PBC::PBC_isError(decompressed_size)) {
                    write(output_fd, decompressed_buffer, decompressed_size);
                    write(output_fd, "\n", 1);
                    last_read.clear();
                } else {
                    if (last_read.empty()) {
                        last_read.append(read_buffer + start_pos, i - start_pos);
                    }
                    last_read.append("\n", 1);
                }
                start_pos = i + 1;
            }
        }
        if (start_pos != bytes_read) {
            last_read.append(read_buffer + start_pos, bytes_read - start_pos);
        }
    }

    if (bytes_read == -1) {
        PBC_LOG(ERROR) << "read failed" << std::endl;
        return -1;
    }

    if (!last_read.empty()) {
        size_t decompressed_size = pbc_compress->DecompressUsingPatternWithLength(
            last_read.c_str(), last_read.length(), decompressed_buffer);
        if (PBC::PBC_isError(decompressed_size)) {
            PBC_LOG(ERROR) << "decompress failed" << std::endl;
            return -1;
        }
        write(output_fd, decompressed_buffer, decompressed_size);
        count++;
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
        close(output_fd);
    }
    delete[] pattern_buffer;
    delete[] decompressed_buffer;
    delete[] read_buffer;
    delete pbc_compress;
    auto decompress_stop_time = std::chrono::steady_clock::now();
    PBC_LOG(INFO)
        << "decompress file cost time: "
        << std::chrono::duration<double>(decompress_stop_time - decompress_start_time).count()
        << "s" << std::endl;
    return 0;
}

int main(int argc, const char* argv[]) {
    if (!ParseOptions(argc, argv)) {
        return -1;
    }

    switch (config.operation) {
        case PBCOperation::NO_OPERATION:
            std::cerr << "no operation is set" << std::endl;
            break;
        case PBCOperation::TRAIN_PATTERN:
            return PBCTrainPattern();
        case PBCOperation::TEST_COMPRESS:
            return PBCTestCompress();
        case PBCOperation::COMPRESS:
            return PBCCompressFile();
        case PBCOperation::DECOMPRESS:
            return PBCFileDecompressFile();
        default:
            std::cerr << "Unknow operation, try " << argv[0] << " --help for more information."
                      << std::endl;
    }
    return -1;
}

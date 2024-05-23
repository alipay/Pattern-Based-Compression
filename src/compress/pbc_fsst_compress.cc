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

#include "compress/pbc_fsst_compress.h"

#include "base/memcpy.h"

namespace PBC {

PBC_FSST_Compress::PBC_FSST_Compress(size_t symbol_size, size_t buffer_size)
    : PBC_Compress(symbol_size, buffer_size) {
    InitSecondaryEncoderResource();
}

PBC_FSST_Compress::~PBC_FSST_Compress() { CleanSecondaryEncoderResource(); }

void PBC_FSST_Compress::InitSecondaryEncoderResource() {}

void PBC_FSST_Compress::CleanSecondaryEncoderResource() {
    if (pbc_fsst_encoder_) {
        pbc_fsst_destroy(pbc_fsst_encoder_);
    }
}

void PBC_FSST_Compress::BuildSecondaryEncoder(const char* data, int64_t data_len,
                                              int64_t data_pos) {
    char* encoder_buffer = new char[data_len - data_pos + 1];
    pbc_memcpy(encoder_buffer, data + data_pos, data_len - data_pos);
    auto enc_ptr = deserializeEncoder(encoder_buffer, data_len - data_pos);
    pbc_fsst_encoder_ = enc_ptr;
    pbc_fsst_decoder_ = pbc_fsst_decoder(pbc_fsst_encoder_);
    delete[] encoder_buffer;
}

size_t PBC_FSST_Compress::ApplySecondaryEncoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    std::vector<uint64_t> compressedRowLens(1);
    std::vector<unsigned char*> compressedRowPtrs(2);
    std::vector<unsigned char> compressionBuffer;
    compressionBuffer.resize(16 + 2 * input_cstring_len);
    uint64_t len_in = input_cstring_len;
    pbc_fsst_compress(pbc_fsst_encoder_, 1, &len_in,
                      reinterpret_cast<unsigned char**>(const_cast<char**>(&input_cstring)),
                      compressionBuffer.size(), compressionBuffer.data(), compressedRowLens.data(),
                      compressedRowPtrs.data());
    uint64_t compressedLen =
        input_cstring_len == 0
            ? 0
            : (compressedRowPtrs[0] + compressedRowLens[0] - compressionBuffer.data());
    max_output_cstring_len = compressionBuffer.size();
    pbc_memcpy(output_cstring, compressionBuffer.data(), compressedLen);

    return compressedLen;
}

size_t PBC_FSST_Compress::ApplySecondaryDecoding(const char* input_cstring, int input_cstring_len,
                                                 char* output_cstring, int max_output_cstring_len) {
    return pbc_fsst_decompress(&pbc_fsst_decoder_, input_cstring_len,
                               reinterpret_cast<unsigned char*>(const_cast<char*>(input_cstring)),
                               max_output_cstring_len,
                               reinterpret_cast<unsigned char*>(output_cstring));
}

uint64_t PBC_FSST_Compress::serializeEncoder(pbc_fsst_encoder_t* enc, char** buffer) {
    // get the Encoder
    Encoder enc_to_ar = *(reinterpret_cast<Encoder*>(enc));
    // to destroy the encoder
    pbc_fsst_encoder_ = enc;

    std::ostringstream os;
    cereal::BinaryOutputArchive archive(os);
    archive(enc_to_ar);

    std::string enc_str = os.str();
    uint64_t buffer_len = enc_str.size();
    *buffer = new char[buffer_len + 1];
    std::copy(enc_str.begin(), enc_str.end(), *buffer);
    *(*buffer + enc_str.size()) = '\0';

    return buffer_len + 1;
}

pbc_fsst_encoder_t* PBC_FSST_Compress::deserializeEncoder(const char* buffer, int64_t buffer_len) {
    std::stringstream ss(std::string(buffer, buffer_len));
    cereal::BinaryInputArchive iarchive(ss);
    auto enc_from_ar = new Encoder();
    iarchive(*enc_from_ar);
    return reinterpret_cast<pbc_fsst_encoder_t*>(enc_from_ar);
}

}  // namespace PBC

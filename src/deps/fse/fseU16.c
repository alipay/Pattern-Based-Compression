/* ******************************************************************
   FSEU16 : Finite State Entropy coder for 16-bits input
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
   - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/* *************************************************************
*  Tuning parameters
*****************************************************************/
/* MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#ifndef FSEU16_MAX_MEMORY_USAGE
#  define FSEU16_MAX_MEMORY_USAGE 15
#endif
#ifndef FSEU16_DEFAULT_MEMORY_USAGE
#  define FSEU16_DEFAULT_MEMORY_USAGE 14
#endif

/* **************************************************************
*  Includes
*****************************************************************/
#include "fseU16.h"
#define FSEU16_SYMBOLVALUE_ABSOLUTEMAX 4095
#if (FSEU16_MAX_SYMBOL_VALUE > FSEU16_SYMBOLVALUE_ABSOLUTEMAX)
#  error "FSEU16_MAX_SYMBOL_VALUE is too large !"
#endif

/* **************************************************************
*  Compiler specifics
*****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#endif

#if defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif

#if defined (__clang__)
#  pragma clang diagnostic ignored "-Wunused-function"
#endif


/* **************************************************************
*  Local type
****************************************************************/
typedef struct {
    unsigned short newState;
    unsigned nbBits : 4;
    unsigned symbol : 12;
} PBC_FSE_decode_tU16;    /* Note : the size of this struct must be 4 */


/* *******************************************************************
*  Include type-specific functions from fse.c (C template emulation)
*********************************************************************/
#define PBC_FSE_COMMONDEFS_ONLY

#ifdef PBC_FSE_MAX_MEMORY_USAGE
#  undef PBC_FSE_MAX_MEMORY_USAGE
#endif
#ifdef PBC_FSE_DEFAULT_MEMORY_USAGE
#  undef PBC_FSE_DEFAULT_MEMORY_USAGE
#endif
#define PBC_FSE_MAX_MEMORY_USAGE FSEU16_MAX_MEMORY_USAGE
#define PBC_FSE_DEFAULT_MEMORY_USAGE FSEU16_DEFAULT_MEMORY_USAGE

#define PBC_FSE_FUNCTION_TYPE U16
#define PBC_FSE_FUNCTION_EXTENSION U16

#define PBC_FSE_count_generic PBC_FSE_count_genericU16
#define PBC_FSE_buildCTable   PBC_FSE_buildCTableU16
#define PBC_FSE_buildCTable_wksp PBC_FSE_buildCTable_wksp_U16

#define PBC_FSE_DECODE_TYPE   PBC_FSE_decode_tU16
#define PBC_FSE_createDTable  PBC_FSE_createDTableU16
#define PBC_FSE_freeDTable    PBC_FSE_freeDTableU16
#define PBC_FSE_buildDTable   PBC_FSE_buildDTableU16

#include "fse_compress.c"   /* PBC_FSE_countU16, PBC_FSE_buildCTableU16 */
#include "fse_decompress.c"   /* PBC_FSE_buildDTableU16 */


/*! PBC_FSE_countU16() :
    This function counts U16 values stored in `src`,
    and push the histogram into `count`.
   @return : count of most common element
   *maxSymbolValuePtr : will be updated with value of highest symbol.
*/
size_t PBC_FSE_countU16(unsigned* count, unsigned* maxSymbolValuePtr,
                    const U16* src, size_t srcSize)
{
    const U16* ip16 = (const U16*)src;
    const U16* const end = src + srcSize;
    unsigned maxSymbolValue = *maxSymbolValuePtr;

    memset(count, 0, (maxSymbolValue+1)*sizeof(*count));
    if (srcSize==0) { *maxSymbolValuePtr = 0; return 0; }

    while (ip16<end) {
        if (*ip16 > maxSymbolValue)
            return ERROR(maxSymbolValue_tooSmall);
        count[*ip16++]++;
    }

    while (!count[maxSymbolValue]) maxSymbolValue--;
    *maxSymbolValuePtr = maxSymbolValue;

    {   U32 s, max=0;
        for (s=0; s<=maxSymbolValue; s++)
            if (count[s] > max) max = count[s];
        return (size_t)max;
    }
}

/* *******************************************************
*  U16 Compression functions
*********************************************************/
size_t PBC_FSE_compressU16_usingCTable (void* dst, size_t maxDstSize,
                              const U16*  src, size_t srcSize,
                              const PBC_FSE_CTable* ct)
{
    const U16* const istart = src;
    const U16* const iend = istart + srcSize;
    const U16* ip;

    BYTE* op = (BYTE*) dst;
    PBC_BIT_CStream_t bitC;
    PBC_FSE_CState_t CState;


    /* init */
    PBC_BIT_initCStream(&bitC, op, maxDstSize);
    PBC_FSE_initCState(&CState, ct);

    ip=iend;

    /* join to even */
    if (srcSize & 1) {
        PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);
        PBC_BIT_flushBits(&bitC);
    }

    /* join to mod 4 */
    if (srcSize & 2) {
        PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);
        PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);
        PBC_BIT_flushBits(&bitC);
    }

    /* 2 or 4 encoding per loop */
    while (ip>istart) {
        PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);

        if (sizeof(size_t)*8 < PBC_FSE_MAX_TABLELOG*2+7 )  /* This test must be static */
            PBC_BIT_flushBits(&bitC);

        PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);

        if (sizeof(size_t)*8 > PBC_FSE_MAX_TABLELOG*4+7 ) {  /* This test must be static */
            PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);
            PBC_FSE_encodeSymbol(&bitC, &CState, *--ip);
        }
        PBC_BIT_flushBits(&bitC);
    }

    PBC_FSE_flushCState(&bitC, &CState);
    return PBC_BIT_closeCStream(&bitC);
}


size_t PBC_FSE_compressU16(void* dst, size_t maxDstSize,
       const unsigned short* src, size_t srcSize,
       unsigned maxSymbolValue, unsigned tableLog)
{
    const U16* const istart = src;
    const U16* ip = istart;

    BYTE* const ostart = (BYTE*) dst;
    BYTE* const omax = ostart + maxDstSize;
    BYTE* op = ostart;

    U32   counting[PBC_FSE_MAX_SYMBOL_VALUE+1] = {0};
    S16   norm[PBC_FSE_MAX_SYMBOL_VALUE+1];

    /* checks */
    if (srcSize <= 1) return srcSize;
    if (!maxSymbolValue) maxSymbolValue = PBC_FSE_MAX_SYMBOL_VALUE;
    if (!tableLog) tableLog = PBC_FSE_DEFAULT_TABLELOG;
    if (maxSymbolValue > PBC_FSE_MAX_SYMBOL_VALUE) return ERROR(maxSymbolValue_tooLarge);
    if (tableLog > PBC_FSE_MAX_TABLELOG) return ERROR(tableLog_tooLarge);

    /* Scan for stats */
    {   size_t const maxCount = PBC_FSE_countU16 (counting, &maxSymbolValue, ip, srcSize);
        if (PBC_FSE_isError(maxCount)) return maxCount;
        if (maxCount == srcSize) return 1;   /* src contains one constant element x srcSize times. Use RLE compression. */
    }
    /* Normalize */
    tableLog = PBC_FSE_optimalTableLog(tableLog, srcSize, maxSymbolValue);
    {   size_t const errorCode = PBC_FSE_normalizeCount (norm, tableLog, counting, srcSize, maxSymbolValue);
        if (PBC_FSE_isError(errorCode)) return errorCode;
    }
    /* Write table description header */
    {   size_t const NSize = PBC_FSE_writeNCount (op, omax-op, norm, maxSymbolValue, tableLog);
        if (PBC_FSE_isError(NSize)) return NSize;
        op += NSize;
    }
    /* Compress */
    {   PBC_FSE_CTable CTable[PBC_FSE_CTABLE_SIZE_U32(PBC_FSE_MAX_TABLELOG, PBC_FSE_MAX_SYMBOL_VALUE)];
        size_t const errorCode = PBC_FSE_buildCTableU16 (CTable, norm, maxSymbolValue, tableLog);
        if (PBC_FSE_isError(errorCode)) return errorCode;
        op += PBC_FSE_compressU16_usingCTable (op, omax - op, ip, srcSize, CTable);
    }

    /* check compressibility */
    if ( (size_t)(op-ostart) >= (size_t)(srcSize-1)*(sizeof(U16)) )
        return 0;   /* no compression */

    return op-ostart;
}


/* *******************************************************
*  U16 Decompression functions
*********************************************************/

U16 PBC_FSE_decodeSymbolU16(PBC_FSE_DState_t* DStatePtr, PBC_BIT_DStream_t* bitD)
{
    const PBC_FSE_decode_tU16 DInfo = ((const PBC_FSE_decode_tU16*)(DStatePtr->table))[DStatePtr->state];
    U16 symbol;
    size_t lowBits;
    const U32 nbBits = DInfo.nbBits;

    symbol = (U16)(DInfo.symbol);
    lowBits = PBC_BIT_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;

    return symbol;
}


size_t PBC_FSE_decompressU16_usingDTable (U16* dst, size_t maxDstSize,
                               const void* cSrc, size_t cSrcSize,
                               const PBC_FSE_DTable* dt)
{
    U16* const ostart = dst;
    U16* op = ostart;
    U16* const oend = ostart + maxDstSize;
    PBC_BIT_DStream_t bitD;
    PBC_FSE_DState_t state;

    /* Init */
    memset(&bitD, 0, sizeof(bitD));
    PBC_BIT_initDStream(&bitD, cSrc, cSrcSize);
    PBC_FSE_initDState(&state, &bitD, dt);

    while((PBC_BIT_reloadDStream(&bitD) < PBC_BIT_DStream_completed) && (op<oend)) {
        *op++ = PBC_FSE_decodeSymbolU16(&state, &bitD);
    }

    if (!PBC_BIT_endOfDStream(&bitD)) return ERROR(corruption_detected);

    while (state.state && op<oend) {
        *op++ = PBC_FSE_decodeSymbolU16(&state, &bitD);
    }

    if (state.state) return ERROR(corruption_detected);

    return op-ostart;
}


typedef PBC_FSE_DTable DTable_max_t[PBC_FSE_DTABLE_SIZE_U32(PBC_FSE_MAX_TABLELOG)];

size_t PBC_FSE_decompressU16(U16* dst, size_t maxDstSize,
                  const void* cSrc, size_t cSrcSize)
{
    const BYTE* const istart = (const BYTE*) cSrc;
    const BYTE* ip = istart;
    short NCount[PBC_FSE_MAX_SYMBOL_VALUE+1];
    DTable_max_t dt;
    unsigned maxSymbolValue = PBC_FSE_MAX_SYMBOL_VALUE;
    unsigned tableLog;

    /* Sanity check */
    if (cSrcSize<2) return ERROR(srcSize_wrong);   /* specific corner cases (uncompressed & rle) */

    /* normal FSE decoding mode */
    {   size_t const NSize = PBC_FSE_readNCount (NCount, &maxSymbolValue, &tableLog, istart, cSrcSize);
        if (PBC_FSE_isError(NSize)) return NSize;
        ip += NSize;
        cSrcSize -= NSize;
    }
    {   size_t const errorCode = PBC_FSE_buildDTableU16 (dt, NCount, maxSymbolValue, tableLog);
        if (PBC_FSE_isError(errorCode)) return errorCode;
    }
    return PBC_FSE_decompressU16_usingDTable (dst, maxDstSize, ip, cSrcSize, dt);
}

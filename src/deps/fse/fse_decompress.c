/* ******************************************************************
 * FSE : Finite State Entropy decoder
 * Copyright (c) 2013-2020, Yann Collet, Facebook, Inc.
 *
 *  You can contact the author at :
 *  - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *  - Public forum : https://groups.google.com/forum/#!forum/lz4c
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */


/* **************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>     /* malloc, free, qsort */
#include <string.h>     /* memcpy, memset */
#include "debug.h"      /* assert */
#include "bitstream.h"
#include "compiler.h"
#define PBC_FSE_STATIC_LINKING_ONLY
#include "fse.h"
#include "error_private.h"


/* **************************************************************
*  Error Management
****************************************************************/
#define PBC_FSE_isError ERR_isError
#define PBC_FSE_STATIC_ASSERT(c) DEBUG_STATIC_ASSERT(c)   /* use only *after* variable declarations */


/* **************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef PBC_FSE_FUNCTION_EXTENSION
#  error "PBC_FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef PBC_FSE_FUNCTION_TYPE
#  error "PBC_FSE_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define PBC_FSE_CAT(X,Y) X##Y
#define PBC_FSE_FUNCTION_NAME(X,Y) PBC_FSE_CAT(X,Y)
#define PBC_FSE_TYPE_NAME(X,Y) PBC_FSE_CAT(X,Y)


/* Function templates */
PBC_FSE_DTable* PBC_FSE_createDTable (unsigned tableLog)
{
    if (tableLog > PBC_FSE_TABLELOG_ABSOLUTE_MAX) tableLog = PBC_FSE_TABLELOG_ABSOLUTE_MAX;
    return (PBC_FSE_DTable*)malloc( PBC_FSE_DTABLE_SIZE_U32(tableLog) * sizeof (U32) );
}

void PBC_FSE_freeDTable (PBC_FSE_DTable* dt)
{
    free(dt);
}

size_t PBC_FSE_buildDTable(PBC_FSE_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    void* const tdPtr = dt+1;   /* because *dt is unsigned, 32-bits aligned on 32-bits */
    PBC_FSE_DECODE_TYPE* const tableDecode = (PBC_FSE_DECODE_TYPE*) (tdPtr);
    U16 symbolNext[PBC_FSE_MAX_SYMBOL_VALUE+1];

    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;
    U32 highThreshold = tableSize-1;

    /* Sanity Checks */
    if (maxSymbolValue > PBC_FSE_MAX_SYMBOL_VALUE) return ERROR(maxSymbolValue_tooLarge);
    if (tableLog > PBC_FSE_MAX_TABLELOG) return ERROR(tableLog_tooLarge);

    /* Init, lay down lowprob symbols */
    {   PBC_FSE_DTableHeader DTableH;
        DTableH.tableLog = (U16)tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].symbol = (PBC_FSE_FUNCTION_TYPE)s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    symbolNext[s] = normalizedCounter[s];
        }   }   }
        memcpy(dt, &DTableH, sizeof(DTableH));
    }

    /* Spread symbols */
    {   U32 const tableMask = tableSize-1;
        U32 const step = PBC_FSE_TABLESTEP(tableSize);
        U32 s, position = 0;
        for (s=0; s<maxSV1; s++) {
            int i;
            for (i=0; i<normalizedCounter[s]; i++) {
                tableDecode[position].symbol = (PBC_FSE_FUNCTION_TYPE)s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }
        if (position!=0) return ERROR(GENERIC);   /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {   U32 u;
        for (u=0; u<tableSize; u++) {
            PBC_FSE_FUNCTION_TYPE const symbol = (PBC_FSE_FUNCTION_TYPE)(tableDecode[u].symbol);
            U32 const nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - PBC_BIT_highbit32(nextState) );
            tableDecode[u].newState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
    }   }

    return 0;
}


#ifndef PBC_FSE_COMMONDEFS_ONLY

/*-*******************************************************
*  Decompression (Byte symbols)
*********************************************************/
size_t PBC_FSE_buildDTable_rle (PBC_FSE_DTable* dt, BYTE symbolValue)
{
    void* ptr = dt;
    PBC_FSE_DTableHeader* const DTableH = (PBC_FSE_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    PBC_FSE_decode_t* const cell = (PBC_FSE_decode_t*)dPtr;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->newState = 0;
    cell->symbol = symbolValue;
    cell->nbBits = 0;

    return 0;
}


size_t PBC_FSE_buildDTable_raw (PBC_FSE_DTable* dt, unsigned nbBits)
{
    void* ptr = dt;
    PBC_FSE_DTableHeader* const DTableH = (PBC_FSE_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    PBC_FSE_decode_t* const dinfo = (PBC_FSE_decode_t*)dPtr;
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSV1 = tableMask+1;
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return ERROR(GENERIC);         /* min size */

    /* Build Decoding Table */
    DTableH->tableLog = (U16)nbBits;
    DTableH->fastMode = 1;
    for (s=0; s<maxSV1; s++) {
        dinfo[s].newState = 0;
        dinfo[s].symbol = (BYTE)s;
        dinfo[s].nbBits = (BYTE)nbBits;
    }

    return 0;
}

FORCE_INLINE_TEMPLATE size_t PBC_FSE_decompress_usingDTable_generic(
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const PBC_FSE_DTable* dt, const unsigned fast)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-3;

    PBC_BIT_DStream_t bitD;
    PBC_FSE_DState_t state1;
    PBC_FSE_DState_t state2;

    /* Init */
    CHECK_F(PBC_BIT_initDStream(&bitD, cSrc, cSrcSize));

    PBC_FSE_initDState(&state1, &bitD, dt);
    PBC_FSE_initDState(&state2, &bitD, dt);

#define PBC_FSE_GETSYMBOL(statePtr) fast ? PBC_FSE_decodeSymbolFast(statePtr, &bitD) : PBC_FSE_decodeSymbol(statePtr, &bitD)

    /* 4 symbols per loop */
    for ( ; (PBC_BIT_reloadDStream(&bitD)==PBC_BIT_DStream_unfinished) & (op<olimit) ; op+=4) {
        op[0] = PBC_FSE_GETSYMBOL(&state1);

        if (PBC_FSE_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            PBC_BIT_reloadDStream(&bitD);

        op[1] = PBC_FSE_GETSYMBOL(&state2);

        if (PBC_FSE_MAX_TABLELOG*4+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            { if (PBC_BIT_reloadDStream(&bitD) > PBC_BIT_DStream_unfinished) { op+=2; break; } }

        op[2] = PBC_FSE_GETSYMBOL(&state1);

        if (PBC_FSE_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            PBC_BIT_reloadDStream(&bitD);

        op[3] = PBC_FSE_GETSYMBOL(&state2);
    }

    /* tail */
    /* note : PBC_BIT_reloadDStream(&bitD) >= PBC_FSE_DStream_partiallyFilled; Ends at exactly PBC_BIT_DStream_completed */
    while (1) {
        if (op>(omax-2)) return ERROR(dstSize_tooSmall);
        *op++ = PBC_FSE_GETSYMBOL(&state1);
        if (PBC_BIT_reloadDStream(&bitD)==PBC_BIT_DStream_overflow) {
            *op++ = PBC_FSE_GETSYMBOL(&state2);
            break;
        }

        if (op>(omax-2)) return ERROR(dstSize_tooSmall);
        *op++ = PBC_FSE_GETSYMBOL(&state2);
        if (PBC_BIT_reloadDStream(&bitD)==PBC_BIT_DStream_overflow) {
            *op++ = PBC_FSE_GETSYMBOL(&state1);
            break;
    }   }

    return op-ostart;
}


size_t PBC_FSE_decompress_usingDTable(void* dst, size_t originalSize,
                            const void* cSrc, size_t cSrcSize,
                            const PBC_FSE_DTable* dt)
{
    const void* ptr = dt;
    const PBC_FSE_DTableHeader* DTableH = (const PBC_FSE_DTableHeader*)ptr;
    const U32 fastMode = DTableH->fastMode;

    /* select fast mode (static) */
    if (fastMode) return PBC_FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
    return PBC_FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}


size_t PBC_FSE_decompress_wksp(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, PBC_FSE_DTable* workSpace, unsigned maxLog)
{
    const BYTE* const istart = (const BYTE*)cSrc;
    const BYTE* ip = istart;
    short counting[PBC_FSE_MAX_SYMBOL_VALUE+1];
    unsigned tableLog;
    unsigned maxSymbolValue = PBC_FSE_MAX_SYMBOL_VALUE;

    /* normal FSE decoding mode */
    size_t const NCountLength = PBC_FSE_readNCount (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
    if (PBC_FSE_isError(NCountLength)) return NCountLength;
    if (tableLog > maxLog) return ERROR(tableLog_tooLarge);
    assert(NCountLength <= cSrcSize);
    ip += NCountLength;
    cSrcSize -= NCountLength;

    CHECK_F( PBC_FSE_buildDTable (workSpace, counting, maxSymbolValue, tableLog) );

    return PBC_FSE_decompress_usingDTable (dst, dstCapacity, ip, cSrcSize, workSpace);   /* always return, even if it is an error code */
}


typedef PBC_FSE_DTable DTable_max_t[PBC_FSE_DTABLE_SIZE_U32(PBC_FSE_MAX_TABLELOG)];

size_t PBC_FSE_decompress(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize)
{
    DTable_max_t dt;   /* Static analyzer seems unable to understand this table will be properly initialized later */
    return PBC_FSE_decompress_wksp(dst, dstCapacity, cSrc, cSrcSize, dt, PBC_FSE_MAX_TABLELOG);
}



#endif   /* PBC_FSE_COMMONDEFS_ONLY */

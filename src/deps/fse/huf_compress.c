/* ******************************************************************
 * Huffman encoder, part of New Generation Entropy library
 * Copyright (c) 2013-2020, Yann Collet, Facebook, Inc.
 *
 *  You can contact the author at :
 *  - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *  - Public forum : https://groups.google.com/forum/#!forum/lz4c
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */

/* **************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#endif


/* **************************************************************
*  Includes
****************************************************************/
#include <string.h>     /* memcpy, memset */
#include <stdio.h>      /* printf (debug) */
#include "compiler.h"
#include "bitstream.h"
#include "hist.h"
#define PBC_FSE_STATIC_LINKING_ONLY   /* PBC_FSE_optimalTableLog_internal */
#include "fse.h"        /* header compression */
#define PBC_HUF_STATIC_LINKING_ONLY
#include "huf.h"
#include "error_private.h"


/* **************************************************************
*  Error Management
****************************************************************/
#define PBC_HUF_isError ERR_isError
#define PBC_HUF_STATIC_ASSERT(c) DEBUG_STATIC_ASSERT(c)   /* use only *after* variable declarations */


/* **************************************************************
*  Utils
****************************************************************/
PBC_HUF_PUBLIC_API unsigned PBC_HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue)
{
    return PBC_FSE_optimalTableLog_internal(maxTableLog, srcSize, maxSymbolValue, 1);
}


/* *******************************************************
*  HUF : Huffman block compression
*********************************************************/
/* PBC_HUF_compressWeights() :
 * Same as PBC_FSE_compress(), but dedicated to huff0's weights compression.
 * The use case needs much less stack memory.
 * Note : all elements within weightTable are supposed to be <= PBC_HUF_TABLELOG_MAX.
 */
#define MAX_PBC_FSE_TABLELOG_FOR_HUFF_HEADER 6
static size_t PBC_HUF_compressWeights (void* dst, size_t dstSize, const void* weightTable, size_t wtSize)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    unsigned maxSymbolValue = PBC_HUF_TABLELOG_MAX;
    U32 tableLog = MAX_PBC_FSE_TABLELOG_FOR_HUFF_HEADER;

    PBC_FSE_CTable CTable[PBC_FSE_CTABLE_SIZE_U32(MAX_PBC_FSE_TABLELOG_FOR_HUFF_HEADER, PBC_HUF_TABLELOG_MAX)];
    BYTE scratchBuffer[1<<MAX_PBC_FSE_TABLELOG_FOR_HUFF_HEADER];

    unsigned count[PBC_HUF_TABLELOG_MAX+1];
    S16 norm[PBC_HUF_TABLELOG_MAX+1];

    /* init conditions */
    if (wtSize <= 1) return 0;  /* Not compressible */

    /* Scan input and build symbol stats */
    {   unsigned const maxCount = PBC_HIST_count_simple(count, &maxSymbolValue, weightTable, wtSize);   /* never fails */
        if (maxCount == wtSize) return 1;   /* only a single symbol in src : rle */
        if (maxCount == 1) return 0;        /* each symbol present maximum once => not compressible */
    }

    tableLog = PBC_FSE_optimalTableLog(tableLog, wtSize, maxSymbolValue);
    CHECK_F( PBC_FSE_normalizeCount(norm, tableLog, count, wtSize, maxSymbolValue) );

    /* Write table description header */
    {   CHECK_V_F(hSize, PBC_FSE_writeNCount(op, (size_t)(oend-op), norm, maxSymbolValue, tableLog) );
        op += hSize;
    }

    /* Compress */
    CHECK_F( PBC_FSE_buildCTable_wksp(CTable, norm, maxSymbolValue, tableLog, scratchBuffer, sizeof(scratchBuffer)) );
    {   CHECK_V_F(cSize, PBC_FSE_compress_usingCTable(op, (size_t)(oend - op), weightTable, wtSize, CTable) );
        if (cSize == 0) return 0;   /* not enough space for compressed data */
        op += cSize;
    }

    return (size_t)(op-ostart);
}


struct PBC_HUF_CElt_s {
  U16  val;
  BYTE nbBits;
};   /* typedef'd to PBC_HUF_CElt within "huf.h" */

/*! PBC_HUF_writeCTable() :
    `CTable` : Huffman tree to save, using huf representation.
    @return : size of saved CTable */
size_t PBC_HUF_writeCTable (void* dst, size_t maxDstSize,
                        const PBC_HUF_CElt* CTable, unsigned maxSymbolValue, unsigned huffLog)
{
    BYTE bitsToWeight[PBC_HUF_TABLELOG_MAX + 1];   /* precomputed conversion table */
    BYTE huffWeight[PBC_HUF_SYMBOLVALUE_MAX];
    BYTE* op = (BYTE*)dst;
    U32 n;

     /* check conditions */
    if (maxSymbolValue > PBC_HUF_SYMBOLVALUE_MAX) return ERROR(maxSymbolValue_tooLarge);

    /* convert to weight */
    bitsToWeight[0] = 0;
    for (n=1; n<huffLog+1; n++)
        bitsToWeight[n] = (BYTE)(huffLog + 1 - n);
    for (n=0; n<maxSymbolValue; n++)
        huffWeight[n] = bitsToWeight[CTable[n].nbBits];

    /* attempt weights compression by FSE */
    {   CHECK_V_F(hSize, PBC_HUF_compressWeights(op+1, maxDstSize-1, huffWeight, maxSymbolValue) );
        if ((hSize>1) & (hSize < maxSymbolValue/2)) {   /* FSE compressed */
            op[0] = (BYTE)hSize;
            return hSize+1;
    }   }

    /* write raw values as 4-bits (max : 15) */
    if (maxSymbolValue > (256-128)) return ERROR(GENERIC);   /* should not happen : likely means source cannot be compressed */
    if (((maxSymbolValue+1)/2) + 1 > maxDstSize) return ERROR(dstSize_tooSmall);   /* not enough space within dst buffer */
    op[0] = (BYTE)(128 /*special case*/ + (maxSymbolValue-1));
    huffWeight[maxSymbolValue] = 0;   /* to be sure it doesn't cause msan issue in final combination */
    for (n=0; n<maxSymbolValue; n+=2)
        op[(n/2)+1] = (BYTE)((huffWeight[n] << 4) + huffWeight[n+1]);
    return ((maxSymbolValue+1)/2) + 1;
}


size_t PBC_HUF_readCTable (PBC_HUF_CElt* CTable, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize, unsigned* hasZeroWeights)
{
    BYTE huffWeight[PBC_HUF_SYMBOLVALUE_MAX + 1];   /* init not required, even though some static analyzer may complain */
    U32 rankVal[PBC_HUF_TABLELOG_ABSOLUTEMAX + 1];   /* large enough for values from 0 to 16 */
    U32 tableLog = 0;
    U32 nbSymbols = 0;

    /* get symbol weights */
    CHECK_V_F(readSize, PBC_HUF_readStats(huffWeight, PBC_HUF_SYMBOLVALUE_MAX+1, rankVal, &nbSymbols, &tableLog, src, srcSize));

    /* check result */
    if (tableLog > PBC_HUF_TABLELOG_MAX) return ERROR(tableLog_tooLarge);
    if (nbSymbols > *maxSymbolValuePtr+1) return ERROR(maxSymbolValue_tooSmall);

    /* Prepare base value per rank */
    {   U32 n, nextRankStart = 0;
        for (n=1; n<=tableLog; n++) {
            U32 current = nextRankStart;
            nextRankStart += (rankVal[n] << (n-1));
            rankVal[n] = current;
    }   }

    /* fill nbBits */
    *hasZeroWeights = 0;
    {   U32 n; for (n=0; n<nbSymbols; n++) {
            const U32 w = huffWeight[n];
            *hasZeroWeights |= (w == 0);
            CTable[n].nbBits = (BYTE)(tableLog + 1 - w) & -(w != 0);
    }   }

    /* fill val */
    {   U16 nbPerRank[PBC_HUF_TABLELOG_MAX+2]  = {0};  /* support w=0=>n=tableLog+1 */
        U16 valPerRank[PBC_HUF_TABLELOG_MAX+2] = {0};
        { U32 n; for (n=0; n<nbSymbols; n++) nbPerRank[CTable[n].nbBits]++; }
        /* determine stating value per rank */
        valPerRank[tableLog+1] = 0;   /* for w==0 */
        {   U16 min = 0;
            U32 n; for (n=tableLog; n>0; n--) {  /* start at n=tablelog <-> w=1 */
                valPerRank[n] = min;     /* get starting value within each rank */
                min += nbPerRank[n];
                min >>= 1;
        }   }
        /* assign value within rank, symbol order */
        { U32 n; for (n=0; n<nbSymbols; n++) CTable[n].val = valPerRank[CTable[n].nbBits]++; }
    }

    *maxSymbolValuePtr = nbSymbols - 1;
    return readSize;
}

U32 PBC_HUF_getNbBits(const void* symbolTable, U32 symbolValue)
{
    const PBC_HUF_CElt* table = (const PBC_HUF_CElt*)symbolTable;
    assert(symbolValue <= PBC_HUF_SYMBOLVALUE_MAX);
    return table[symbolValue].nbBits;
}


typedef struct nodeElt_s {
    U32 count;
    U16 parent;
    BYTE byte;
    BYTE nbBits;
} nodeElt;

static U32 PBC_HUF_setMaxHeight(nodeElt* huffNode, U32 lastNonNull, U32 maxNbBits)
{
    const U32 largestBits = huffNode[lastNonNull].nbBits;
    if (largestBits <= maxNbBits) return largestBits;   /* early exit : no elt > maxNbBits */

    /* there are several too large elements (at least >= 2) */
    {   int totalCost = 0;
        const U32 baseCost = 1 << (largestBits - maxNbBits);
        int n = (int)lastNonNull;

        while (huffNode[n].nbBits > maxNbBits) {
            totalCost += baseCost - (1 << (largestBits - huffNode[n].nbBits));
            huffNode[n].nbBits = (BYTE)maxNbBits;
            n --;
        }  /* n stops at huffNode[n].nbBits <= maxNbBits */
        while (huffNode[n].nbBits == maxNbBits) n--;   /* n end at index of smallest symbol using < maxNbBits */

        /* renorm totalCost */
        totalCost >>= (largestBits - maxNbBits);  /* note : totalCost is necessarily a multiple of baseCost */

        /* repay normalized cost */
        {   U32 const noSymbol = 0xF0F0F0F0;
            U32 rankLast[PBC_HUF_TABLELOG_MAX+2];

            /* Get pos of last (smallest) symbol per rank */
            memset(rankLast, 0xF0, sizeof(rankLast));
            {   U32 currentNbBits = maxNbBits;
                int pos;
                for (pos=n ; pos >= 0; pos--) {
                    if (huffNode[pos].nbBits >= currentNbBits) continue;
                    currentNbBits = huffNode[pos].nbBits;   /* < maxNbBits */
                    rankLast[maxNbBits-currentNbBits] = (U32)pos;
            }   }

            while (totalCost > 0) {
                U32 nBitsToDecrease = PBC_BIT_highbit32((U32)totalCost) + 1;
                for ( ; nBitsToDecrease > 1; nBitsToDecrease--) {
                    U32 const highPos = rankLast[nBitsToDecrease];
                    U32 const lowPos = rankLast[nBitsToDecrease-1];
                    if (highPos == noSymbol) continue;
                    if (lowPos == noSymbol) break;
                    {   U32 const highTotal = huffNode[highPos].count;
                        U32 const lowTotal = 2 * huffNode[lowPos].count;
                        if (highTotal <= lowTotal) break;
                }   }
                /* only triggered when no more rank 1 symbol left => find closest one (note : there is necessarily at least one !) */
                /* PBC_HUF_MAX_TABLELOG test just to please gcc 5+; but it should not be necessary */
                while ((nBitsToDecrease<=PBC_HUF_TABLELOG_MAX) && (rankLast[nBitsToDecrease] == noSymbol))
                    nBitsToDecrease ++;
                totalCost -= 1 << (nBitsToDecrease-1);
                if (rankLast[nBitsToDecrease-1] == noSymbol)
                    rankLast[nBitsToDecrease-1] = rankLast[nBitsToDecrease];   /* this rank is no longer empty */
                huffNode[rankLast[nBitsToDecrease]].nbBits ++;
                if (rankLast[nBitsToDecrease] == 0)    /* special case, reached largest symbol */
                    rankLast[nBitsToDecrease] = noSymbol;
                else {
                    rankLast[nBitsToDecrease]--;
                    if (huffNode[rankLast[nBitsToDecrease]].nbBits != maxNbBits-nBitsToDecrease)
                        rankLast[nBitsToDecrease] = noSymbol;   /* this rank is now empty */
            }   }   /* while (totalCost > 0) */

            while (totalCost < 0) {  /* Sometimes, cost correction overshoot */
                if (rankLast[1] == noSymbol) {  /* special case : no rank 1 symbol (using maxNbBits-1); let's create one from largest rank 0 (using maxNbBits) */
                    while (huffNode[n].nbBits == maxNbBits) n--;
                    huffNode[n+1].nbBits--;
                    assert(n >= 0);
                    rankLast[1] = (U32)(n+1);
                    totalCost++;
                    continue;
                }
                huffNode[ rankLast[1] + 1 ].nbBits--;
                rankLast[1]++;
                totalCost ++;
    }   }   }   /* there are several too large elements (at least >= 2) */

    return maxNbBits;
}

typedef struct {
    U32 base;
    U32 current;
} rankPos;

typedef nodeElt huffNodeTable[PBC_HUF_CTABLE_WORKSPACE_SIZE_U32];

#define RANK_POSITION_TABLE_SIZE 32

typedef struct {
  huffNodeTable huffNodeTbl;
  rankPos rankPosition[RANK_POSITION_TABLE_SIZE];
} PBC_HUF_buildCTable_wksp_tables;

static void PBC_HUF_sort(nodeElt* huffNode, const unsigned* count, U32 maxSymbolValue, rankPos* rankPosition)
{
    U32 n;

    memset(rankPosition, 0, sizeof(*rankPosition) * RANK_POSITION_TABLE_SIZE);
    for (n=0; n<=maxSymbolValue; n++) {
        U32 r = PBC_BIT_highbit32(count[n] + 1);
        rankPosition[r].base ++;
    }
    for (n=30; n>0; n--) rankPosition[n-1].base += rankPosition[n].base;
    for (n=0; n<32; n++) rankPosition[n].current = rankPosition[n].base;
    for (n=0; n<=maxSymbolValue; n++) {
        U32 const c = count[n];
        U32 const r = PBC_BIT_highbit32(c+1) + 1;
        U32 pos = rankPosition[r].current++;
        while ((pos > rankPosition[r].base) && (c > huffNode[pos-1].count)) {
            huffNode[pos] = huffNode[pos-1];
            pos--;
        }
        huffNode[pos].count = c;
        huffNode[pos].byte  = (BYTE)n;
    }
}


/** PBC_HUF_buildCTable_wksp() :
 *  Same as PBC_HUF_buildCTable(), but using externally allocated scratch buffer.
 *  `workSpace` must be aligned on 4-bytes boundaries, and be at least as large as sizeof(PBC_HUF_buildCTable_wksp_tables).
 */
#define STARTNODE (PBC_HUF_SYMBOLVALUE_MAX+1)

size_t PBC_HUF_buildCTable_wksp (PBC_HUF_CElt* tree, const unsigned* count, U32 maxSymbolValue, U32 maxNbBits, void* workSpace, size_t wkspSize)
{
    PBC_HUF_buildCTable_wksp_tables* const wksp_tables = (PBC_HUF_buildCTable_wksp_tables*)workSpace;
    nodeElt* const huffNode0 = wksp_tables->huffNodeTbl;
    nodeElt* const huffNode = huffNode0+1;
    int nonNullRank;
    int lowS, lowN;
    int nodeNb = STARTNODE;
    int n, nodeRoot;

    /* safety checks */
    if (((size_t)workSpace & 3) != 0) return ERROR(GENERIC);  /* must be aligned on 4-bytes boundaries */
    if (wkspSize < sizeof(PBC_HUF_buildCTable_wksp_tables))
      return ERROR(workSpace_tooSmall);
    if (maxNbBits == 0) maxNbBits = PBC_HUF_TABLELOG_DEFAULT;
    if (maxSymbolValue > PBC_HUF_SYMBOLVALUE_MAX)
      return ERROR(maxSymbolValue_tooLarge);
    memset(huffNode0, 0, sizeof(huffNodeTable));

    /* sort, decreasing order */
    PBC_HUF_sort(huffNode, count, maxSymbolValue, wksp_tables->rankPosition);

    /* init for parents */
    nonNullRank = (int)maxSymbolValue;
    while(huffNode[nonNullRank].count == 0) nonNullRank--;
    lowS = nonNullRank; nodeRoot = nodeNb + lowS - 1; lowN = nodeNb;
    huffNode[nodeNb].count = huffNode[lowS].count + huffNode[lowS-1].count;
    huffNode[lowS].parent = huffNode[lowS-1].parent = (U16)nodeNb;
    nodeNb++; lowS-=2;
    for (n=nodeNb; n<=nodeRoot; n++) huffNode[n].count = (U32)(1U<<30);
    huffNode0[0].count = (U32)(1U<<31);  /* fake entry, strong barrier */

    /* create parents */
    while (nodeNb <= nodeRoot) {
        int const n1 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        int const n2 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
        huffNode[nodeNb].count = huffNode[n1].count + huffNode[n2].count;
        huffNode[n1].parent = huffNode[n2].parent = (U16)nodeNb;
        nodeNb++;
    }

    /* distribute weights (unlimited tree height) */
    huffNode[nodeRoot].nbBits = 0;
    for (n=nodeRoot-1; n>=STARTNODE; n--)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;
    for (n=0; n<=nonNullRank; n++)
        huffNode[n].nbBits = huffNode[ huffNode[n].parent ].nbBits + 1;

    /* enforce maxTableLog */
    maxNbBits = PBC_HUF_setMaxHeight(huffNode, (U32)nonNullRank, maxNbBits);

    /* fill result into tree (val, nbBits) */
    {   U16 nbPerRank[PBC_HUF_TABLELOG_MAX+1] = {0};
        U16 valPerRank[PBC_HUF_TABLELOG_MAX+1] = {0};
        int const alphabetSize = (int)(maxSymbolValue + 1);
        if (maxNbBits > PBC_HUF_TABLELOG_MAX) return ERROR(GENERIC);   /* check fit into table */
        for (n=0; n<=nonNullRank; n++)
            nbPerRank[huffNode[n].nbBits]++;
        /* determine stating value per rank */
        {   U16 min = 0;
            for (n=(int)maxNbBits; n>0; n--) {
                valPerRank[n] = min;      /* get starting value within each rank */
                min += nbPerRank[n];
                min >>= 1;
        }   }
        for (n=0; n<alphabetSize; n++)
            tree[huffNode[n].byte].nbBits = huffNode[n].nbBits;   /* push nbBits per symbol, symbol order */
        for (n=0; n<alphabetSize; n++)
            tree[n].val = valPerRank[tree[n].nbBits]++;   /* assign value within rank, symbol order */
    }

    return maxNbBits;
}

/** PBC_HUF_buildCTable() :
 * @return : maxNbBits
 *  Note : count is used before tree is written, so they can safely overlap
 */
size_t PBC_HUF_buildCTable (PBC_HUF_CElt* tree, const unsigned* count, unsigned maxSymbolValue, unsigned maxNbBits)
{
    PBC_HUF_buildCTable_wksp_tables workspace;
    return PBC_HUF_buildCTable_wksp(tree, count, maxSymbolValue, maxNbBits, &workspace, sizeof(workspace));
}

size_t PBC_HUF_estimateCompressedSize(const PBC_HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue)
{
    size_t nbBits = 0;
    int s;
    for (s = 0; s <= (int)maxSymbolValue; ++s) {
        nbBits += CTable[s].nbBits * count[s];
    }
    return nbBits >> 3;
}

int PBC_HUF_validateCTable(const PBC_HUF_CElt* CTable, const unsigned* count, unsigned maxSymbolValue) {
  int bad = 0;
  int s;
  for (s = 0; s <= (int)maxSymbolValue; ++s) {
    bad |= (count[s] != 0) & (CTable[s].nbBits == 0);
  }
  return !bad;
}

size_t PBC_HUF_compressBound(size_t size) { return PBC_HUF_COMPRESSBOUND(size); }

FORCE_INLINE_TEMPLATE void
PBC_HUF_encodeSymbol(PBC_BIT_CStream_t* bitCPtr, U32 symbol, const PBC_HUF_CElt* CTable)
{
    PBC_BIT_addBitsFast(bitCPtr, CTable[symbol].val, CTable[symbol].nbBits);
}

#define PBC_HUF_FLUSHBITS(s)  PBC_BIT_flushBits(s)

#define PBC_HUF_FLUSHBITS_1(stream) \
    if (sizeof((stream)->bitContainer)*8 < PBC_HUF_TABLELOG_MAX*2+7) PBC_HUF_FLUSHBITS(stream)

#define PBC_HUF_FLUSHBITS_2(stream) \
    if (sizeof((stream)->bitContainer)*8 < PBC_HUF_TABLELOG_MAX*4+7) PBC_HUF_FLUSHBITS(stream)

FORCE_INLINE_TEMPLATE size_t
PBC_HUF_compress1X_usingCTable_internal_body(void* dst, size_t dstSize,
                                   const void* src, size_t srcSize,
                                   const PBC_HUF_CElt* CTable)
{
    const BYTE* ip = (const BYTE*) src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;
    size_t n;
    PBC_BIT_CStream_t bitC;

    /* init */
    if (dstSize < 8) return 0;   /* not enough space to compress */
    { size_t const initErr = PBC_BIT_initCStream(&bitC, op, (size_t)(oend-op));
      if (PBC_HUF_isError(initErr)) return 0; }

    n = srcSize & ~3;  /* join to mod 4 */
    switch (srcSize & 3)
    {
        case 3 : PBC_HUF_encodeSymbol(&bitC, ip[n+ 2], CTable);
                 PBC_HUF_FLUSHBITS_2(&bitC);
		 /* fall-through */
        case 2 : PBC_HUF_encodeSymbol(&bitC, ip[n+ 1], CTable);
                 PBC_HUF_FLUSHBITS_1(&bitC);
		 /* fall-through */
        case 1 : PBC_HUF_encodeSymbol(&bitC, ip[n+ 0], CTable);
                 PBC_HUF_FLUSHBITS(&bitC);
		 /* fall-through */
        case 0 : /* fall-through */
        default: break;
    }

    for (; n>0; n-=4) {  /* note : n&3==0 at this stage */
        PBC_HUF_encodeSymbol(&bitC, ip[n- 1], CTable);
        PBC_HUF_FLUSHBITS_1(&bitC);
        PBC_HUF_encodeSymbol(&bitC, ip[n- 2], CTable);
        PBC_HUF_FLUSHBITS_2(&bitC);
        PBC_HUF_encodeSymbol(&bitC, ip[n- 3], CTable);
        PBC_HUF_FLUSHBITS_1(&bitC);
        PBC_HUF_encodeSymbol(&bitC, ip[n- 4], CTable);
        PBC_HUF_FLUSHBITS(&bitC);
    }

    return PBC_BIT_closeCStream(&bitC);
}

#if DYNAMIC_BMI2

static TARGET_ATTRIBUTE("bmi2") size_t
PBC_HUF_compress1X_usingCTable_internal_bmi2(void* dst, size_t dstSize,
                                   const void* src, size_t srcSize,
                                   const PBC_HUF_CElt* CTable)
{
    return PBC_HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

static size_t
PBC_HUF_compress1X_usingCTable_internal_default(void* dst, size_t dstSize,
                                      const void* src, size_t srcSize,
                                      const PBC_HUF_CElt* CTable)
{
    return PBC_HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

static size_t
PBC_HUF_compress1X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const PBC_HUF_CElt* CTable, const int bmi2)
{
    if (bmi2) {
        return PBC_HUF_compress1X_usingCTable_internal_bmi2(dst, dstSize, src, srcSize, CTable);
    }
    return PBC_HUF_compress1X_usingCTable_internal_default(dst, dstSize, src, srcSize, CTable);
}

#else

static size_t
PBC_HUF_compress1X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const PBC_HUF_CElt* CTable, const int bmi2)
{
    (void)bmi2;
    return PBC_HUF_compress1X_usingCTable_internal_body(dst, dstSize, src, srcSize, CTable);
}

#endif

size_t PBC_HUF_compress1X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const PBC_HUF_CElt* CTable)
{
    return PBC_HUF_compress1X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
}


static size_t
PBC_HUF_compress4X_usingCTable_internal(void* dst, size_t dstSize,
                              const void* src, size_t srcSize,
                              const PBC_HUF_CElt* CTable, int bmi2)
{
    size_t const segmentSize = (srcSize+3)/4;   /* first 3 segments */
    const BYTE* ip = (const BYTE*) src;
    const BYTE* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE*) dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;

    if (dstSize < 6 + 1 + 1 + 1 + 8) return 0;   /* minimum space to compress successfully */
    if (srcSize < 12) return 0;   /* no saving possible : too small input */
    op += 6;   /* jumpTable */

    assert(op <= oend);
    {   CHECK_V_F(cSize, PBC_HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        PBC_MEM_writeLE16(ostart, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, PBC_HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        PBC_MEM_writeLE16(ostart+2, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    {   CHECK_V_F(cSize, PBC_HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, segmentSize, CTable, bmi2) );
        if (cSize==0) return 0;
        assert(cSize <= 65535);
        PBC_MEM_writeLE16(ostart+4, (U16)cSize);
        op += cSize;
    }

    ip += segmentSize;
    assert(op <= oend);
    assert(ip <= iend);
    {   CHECK_V_F(cSize, PBC_HUF_compress1X_usingCTable_internal(op, (size_t)(oend-op), ip, (size_t)(iend-ip), CTable, bmi2) );
        if (cSize==0) return 0;
        op += cSize;
    }

    return (size_t)(op-ostart);
}

size_t PBC_HUF_compress4X_usingCTable(void* dst, size_t dstSize, const void* src, size_t srcSize, const PBC_HUF_CElt* CTable)
{
    return PBC_HUF_compress4X_usingCTable_internal(dst, dstSize, src, srcSize, CTable, /* bmi2 */ 0);
}

typedef enum { PBC_HUF_singleStream, PBC_HUF_fourStreams } PBC_HUF_nbStreams_e;

static size_t PBC_HUF_compressCTable_internal(
                BYTE* const ostart, BYTE* op, BYTE* const oend,
                const void* src, size_t srcSize,
                PBC_HUF_nbStreams_e nbStreams, const PBC_HUF_CElt* CTable, const int bmi2)
{
    size_t const cSize = (nbStreams==PBC_HUF_singleStream) ?
                         PBC_HUF_compress1X_usingCTable_internal(op, (size_t)(oend - op), src, srcSize, CTable, bmi2) :
                         PBC_HUF_compress4X_usingCTable_internal(op, (size_t)(oend - op), src, srcSize, CTable, bmi2);
    if (PBC_HUF_isError(cSize)) { return cSize; }
    if (cSize==0) { return 0; }   /* uncompressible */
    op += cSize;
    /* check compressibility */
    assert(op >= ostart);
    if ((size_t)(op-ostart) >= srcSize-1) { return 0; }
    return (size_t)(op-ostart);
}

typedef struct {
    unsigned count[PBC_HUF_SYMBOLVALUE_MAX + 1];
    PBC_HUF_CElt CTable[PBC_HUF_SYMBOLVALUE_MAX + 1];
    PBC_HUF_buildCTable_wksp_tables buildCTable_wksp;
} PBC_HUF_compress_tables_t;

/* PBC_HUF_compress_internal() :
 * `workSpace` must a table of at least PBC_HUF_WORKSPACE_SIZE_U32 unsigned */
static size_t
PBC_HUF_compress_internal (void* dst, size_t dstSize,
                 const void* src, size_t srcSize,
                       unsigned maxSymbolValue, unsigned huffLog,
                       PBC_HUF_nbStreams_e nbStreams,
                       void* workSpace, size_t wkspSize,
                       PBC_HUF_CElt* oldHufTable, PBC_HUF_repeat* repeat, int preferRepeat,
                 const int bmi2)
{
    PBC_HUF_compress_tables_t* const table = (PBC_HUF_compress_tables_t*)workSpace;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstSize;
    BYTE* op = ostart;

    PBC_HUF_STATIC_ASSERT(sizeof(*table) <= PBC_HUF_WORKSPACE_SIZE);

    /* checks & inits */
    if (((size_t)workSpace & 3) != 0) return ERROR(GENERIC);  /* must be aligned on 4-bytes boundaries */
    if (wkspSize < PBC_HUF_WORKSPACE_SIZE) return ERROR(workSpace_tooSmall);
    if (!srcSize) return 0;  /* Uncompressed */
    if (!dstSize) return 0;  /* cannot fit anything within dst budget */
    if (srcSize > PBC_HUF_BLOCKSIZE_MAX) return ERROR(srcSize_wrong);   /* current block size limit */
    if (huffLog > PBC_HUF_TABLELOG_MAX) return ERROR(tableLog_tooLarge);
    if (maxSymbolValue > PBC_HUF_SYMBOLVALUE_MAX) return ERROR(maxSymbolValue_tooLarge);
    if (!maxSymbolValue) maxSymbolValue = PBC_HUF_SYMBOLVALUE_MAX;
    if (!huffLog) huffLog = PBC_HUF_TABLELOG_DEFAULT;

    /* Heuristic : If old table is valid, use it for small inputs */
    if (preferRepeat && repeat && *repeat == PBC_HUF_repeat_valid) {
        return PBC_HUF_compressCTable_internal(ostart, op, oend,
                                           src, srcSize,
                                           nbStreams, oldHufTable, bmi2);
    }

    /* Scan input and build symbol stats */
    {   CHECK_V_F(largest, PBC_HIST_count_wksp (table->count, &maxSymbolValue, (const BYTE*)src, srcSize, workSpace, wkspSize) );
        if (largest == srcSize) { *ostart = ((const BYTE*)src)[0]; return 1; }   /* single symbol, rle */
        if (largest <= (srcSize >> 7)+4) return 0;   /* heuristic : probably not compressible enough */
    }

    /* Check validity of previous table */
    if ( repeat
      && *repeat == PBC_HUF_repeat_check
      && !PBC_HUF_validateCTable(oldHufTable, table->count, maxSymbolValue)) {
        *repeat = PBC_HUF_repeat_none;
    }
    /* Heuristic : use existing table for small inputs */
    if (preferRepeat && repeat && *repeat != PBC_HUF_repeat_none) {
        return PBC_HUF_compressCTable_internal(ostart, op, oend,
                                           src, srcSize,
                                           nbStreams, oldHufTable, bmi2);
    }

    /* Build Huffman Tree */
    huffLog = PBC_HUF_optimalTableLog(huffLog, srcSize, maxSymbolValue);
    {   size_t const maxBits = PBC_HUF_buildCTable_wksp(table->CTable, table->count,
                                            maxSymbolValue, huffLog,
                                            &table->buildCTable_wksp, sizeof(table->buildCTable_wksp));
        CHECK_F(maxBits);
        huffLog = (U32)maxBits;
        /* Zero unused symbols in CTable, so we can check it for validity */
        memset(table->CTable + (maxSymbolValue + 1), 0,
               sizeof(table->CTable) - ((maxSymbolValue + 1) * sizeof(PBC_HUF_CElt)));
    }

    /* Write table description header */
    {   CHECK_V_F(hSize, PBC_HUF_writeCTable (op, dstSize, table->CTable, maxSymbolValue, huffLog) );
        /* Check if using previous huffman table is beneficial */
        if (repeat && *repeat != PBC_HUF_repeat_none) {
            size_t const oldSize = PBC_HUF_estimateCompressedSize(oldHufTable, table->count, maxSymbolValue);
            size_t const newSize = PBC_HUF_estimateCompressedSize(table->CTable, table->count, maxSymbolValue);
            if (oldSize <= hSize + newSize || hSize + 12 >= srcSize) {
                return PBC_HUF_compressCTable_internal(ostart, op, oend,
                                                   src, srcSize,
                                                   nbStreams, oldHufTable, bmi2);
        }   }

        /* Use the new huffman table */
        if (hSize + 12ul >= srcSize) { return 0; }
        op += hSize;
        if (repeat) { *repeat = PBC_HUF_repeat_none; }
        if (oldHufTable)
            memcpy(oldHufTable, table->CTable, sizeof(table->CTable));  /* Save new table */
    }
    return PBC_HUF_compressCTable_internal(ostart, op, oend,
                                       src, srcSize,
                                       nbStreams, table->CTable, bmi2);
}


size_t PBC_HUF_compress1X_wksp (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize)
{
    return PBC_HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, PBC_HUF_singleStream,
                                 workSpace, wkspSize,
                                 NULL, NULL, 0, 0 /*bmi2*/);
}

size_t PBC_HUF_compress1X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      PBC_HUF_CElt* hufTable, PBC_HUF_repeat* repeat, int preferRepeat, int bmi2)
{
    return PBC_HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, PBC_HUF_singleStream,
                                 workSpace, wkspSize, hufTable,
                                 repeat, preferRepeat, bmi2);
}

size_t PBC_HUF_compress1X (void* dst, size_t dstSize,
                 const void* src, size_t srcSize,
                 unsigned maxSymbolValue, unsigned huffLog)
{
    unsigned workSpace[PBC_HUF_WORKSPACE_SIZE_U32];
    return PBC_HUF_compress1X_wksp(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, workSpace, sizeof(workSpace));
}

/* PBC_HUF_compress4X_repeat():
 * compress input using 4 streams.
 * provide workspace to generate compression tables */
size_t PBC_HUF_compress4X_wksp (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize)
{
    return PBC_HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, PBC_HUF_fourStreams,
                                 workSpace, wkspSize,
                                 NULL, NULL, 0, 0 /*bmi2*/);
}

/* PBC_HUF_compress4X_repeat():
 * compress input using 4 streams.
 * re-use an existing huffman compression table */
size_t PBC_HUF_compress4X_repeat (void* dst, size_t dstSize,
                      const void* src, size_t srcSize,
                      unsigned maxSymbolValue, unsigned huffLog,
                      void* workSpace, size_t wkspSize,
                      PBC_HUF_CElt* hufTable, PBC_HUF_repeat* repeat, int preferRepeat, int bmi2)
{
    return PBC_HUF_compress_internal(dst, dstSize, src, srcSize,
                                 maxSymbolValue, huffLog, PBC_HUF_fourStreams,
                                 workSpace, wkspSize,
                                 hufTable, repeat, preferRepeat, bmi2);
}

size_t PBC_HUF_compress2 (void* dst, size_t dstSize,
                const void* src, size_t srcSize,
                unsigned maxSymbolValue, unsigned huffLog)
{
    unsigned workSpace[PBC_HUF_WORKSPACE_SIZE_U32];
    return PBC_HUF_compress4X_wksp(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, workSpace, sizeof(workSpace));
}

size_t PBC_HUF_compress (void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    return PBC_HUF_compress2(dst, maxDstSize, src, srcSize, 255, PBC_HUF_TABLELOG_DEFAULT);
}

/******************************************************************************
 *
 * Copyright (c) 2023, Len Norton
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#ifndef ELIMINATE_H_INCLUDED
#define ELIMINATE_H_INCLUDED

#include "gdal.h"

CPL_C_START

typedef enum
{
    ELIMINATE_MERGE_LARGEST_AREA = 1,
    ELIMINATE_MERGE_SMALLEST_AREA,
    ELIMINATE_MERGE_LONGEST_BOUNDARY
} EliminateMergeType;

typedef struct
{
    char *pszSrcFilename;
    char *pszSrcLayerName;
    char *pszDstFilename;
    char *pszDstLayerName;
    char *pszFormat;
    char *pszWhere;
    EliminateMergeType eMergeType;
} EliminateOptions;

EliminateOptions *EliminateOptionsNew();
void EliminateOptionsFree(EliminateOptions *psOptions);

OGRErr EliminatePolygonsWithOptions(EliminateOptions *psOptions);
OGRErr EliminatePolygons(GDALDatasetH hSrcDS, const char *pszSrcLayerName, GDALDatasetH hDstDS, const char *pszDstLayerName, EliminateMergeType eMergeType, const char *pszWhere);
OGRErr EliminatePolygonsByQuery(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, const char *pszWhere);
OGRErr EliminatePolygonsByFIDStrList(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, char **papszEliminateFIDs);
OGRErr EliminatePolygonsByFID(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, GIntBig *panEliminateFIDs, int nCount);

CPL_C_END

#endif // ELIMINATE_H_INCLUDED

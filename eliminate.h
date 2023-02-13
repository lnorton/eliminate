#ifndef ELIMINATE_H_INCLUDED
#define ELIMINATE_H_INCLUDED

#include "gdal.h"

CPL_C_START

typedef struct
{
    char *pszSrcFilename;
    char *pszSrcLayerName;
    char *pszDstFilename;
    char *pszDstLayerName;
    char *pszFormat;
    char *pszWhere;
} EliminateOptions;

EliminateOptions *EliminateOptionsNew();
void EliminateOptionsFree(EliminateOptions *psOptions);

OGRErr EliminatePolygonsWithOptions(EliminateOptions *psOptions);
OGRErr EliminatePolygonsWithOptionsEx(GDALDatasetH hSrcDS, const char *pszSrcLayerName, GDALDatasetH hDstDS, const char *pszDstLayerName, EliminateOptions *psOptions);
OGRErr EliminatePolygonsByQuery(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, const char *pszWhere);
OGRErr EliminatePolygonsByFID(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, char **papszEliminateFIDs);

CPL_C_END


#endif // ELIMINATE_H_INCLUDED

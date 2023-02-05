#ifndef ELIMINATE_H_INCLUDED
#define ELIMINATE_H_INCLUDED

#include "gdal.h"

CPL_C_START

OGRErr EliminatePolygons(GDALDatasetH hSrcDS, const char* pszSrcLayerName, GDALDatasetH hDstDS, const char* pszDstLayerName);

CPL_C_END

#endif // ELIMINATE_H_INCLUDED

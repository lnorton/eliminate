#ifndef EXPLODE_H_INCLUDED
#define EXPLODE_H_INCLUDED

#include "gdal.h"

CPL_C_START

OGRErr Explode(GDALDatasetH hSrcDS, const char *pszSrcLayerName, GDALDatasetH hDstDS, const char *pszDstLayerName);

CPL_C_END

#endif // EXPLODE_H_INCLUDED

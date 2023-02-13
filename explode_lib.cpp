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

#include "gdal.h"
#include "ogrsf_frmts.h"

#include "explode.h"


static bool IsGeomTypeSupported(OGRwkbGeometryType eType)
{
    switch (eType)
    {
        case wkbPoint:
        case wkbLineString:
        case wkbPolygon:
        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
            return true;
        default:
            return false;
    }
}

static bool IsGeomTypeMulti(OGRwkbGeometryType eType)
{
    switch (eType)
    {
        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
            return true;
        default:
            return false;
    }
}

static OGRwkbGeometryType MultiGeomTypeToSingle(OGRwkbGeometryType eType)
{
    switch (eType)
    {
        case wkbMultiPoint:
            return wkbPoint;
        case wkbMultiLineString:
            return wkbLineString;
        case wkbMultiPolygon:
            return wkbPolygon;
        default:
            return eType;
    }
}

OGRErr CopyFeature(OGRLayer *poDstLayer, const OGRFeature *poSrcFeature, const OGRGeometry *poGeometry)
{
    OGRFeature oDstFeature(poDstLayer->GetLayerDefn());
    for (int iField = 0, nCount = poDstLayer->GetLayerDefn()->GetFieldCount(); iField < nCount; iField++)
    {
        oDstFeature[iField] = (*poSrcFeature)[iField];
    }
    oDstFeature.SetGeometry(poGeometry);
    return poDstLayer->CreateFeature(&oDstFeature);
}

OGRErr Explode(GDALDatasetH hSrcDS, const char *pszSrcLayerName, GDALDatasetH hDstDS, const char *pszDstLayerName)
{
    GDALDataset *poSrcDS = GDALDataset::FromHandle(hSrcDS);
    GDALDataset *poDstDS = GDALDataset::FromHandle(hDstDS);

    OGRLayer *poSrcLayer = nullptr;

    if (pszSrcLayerName == nullptr)
    {
        int nLayers = poSrcDS->GetLayerCount();
        if (nLayers != 1)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Source layer must be specified.");
            return OGRERR_FAILURE;
        }
        else
        {
            poSrcLayer = poSrcDS->GetLayer(0);
        }
    }
    else
    {
        poSrcLayer = poSrcDS->GetLayerByName(pszSrcLayerName);
        if (poSrcLayer == nullptr)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Source layer '%s' not found.", pszSrcLayerName);
            return OGRERR_FAILURE;
        }
    }

    OGRFeatureDefn *poSrcLayerDefn = poSrcLayer->GetLayerDefn();
    int nGeomFieldCount = poSrcLayerDefn->GetGeomFieldCount();
    if (nGeomFieldCount == 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Geometry column not found.");
        return OGRERR_UNSUPPORTED_OPERATION;
    }
    else if (nGeomFieldCount != 1)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Multiple geometry columns not supported.");
        return OGRERR_UNSUPPORTED_OPERATION;
    }

    if (pszDstLayerName == nullptr)
    {
        // TODO: Generate unique name if source and destination are the same.
        pszDstLayerName = poSrcLayer->GetName();
    }

    // TODO: Ownership of layer?

    OGRLayer *poDstLayer = poDstDS->CreateLayer(pszDstLayerName, poSrcLayer->GetSpatialRef(), wkbPolygon);

    for (int iField = 0, nCount = poSrcLayerDefn->GetFieldCount(); iField < nCount; iField++)
    {
        OGRFieldDefn *poSrcFieldDefn = poSrcLayerDefn->GetFieldDefn(iField);
        poDstLayer->CreateField(poSrcFieldDefn);
    }

    OGRGeomFieldDefn *poSrcFieldDefn = poSrcLayerDefn->GetGeomFieldDefn(0);
    OGRwkbGeometryType eSrcType = poSrcFieldDefn->GetType();

    if (!IsGeomTypeSupported(eSrcType))
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Unsupported geometry type '%s'.", OGRGeometryTypeToName(eSrcType));
        return OGRERR_UNSUPPORTED_GEOMETRY_TYPE;
    }

    if (poDstLayer->GetLayerDefn()->GetGeomFieldCount() == 0)
    {
        OGRGeomFieldDefn oDstFieldDefn(poSrcFieldDefn);
        OGRwkbGeometryType eDstType = MultiGeomTypeToSingle(eSrcType);
        oDstFieldDefn.SetType(eDstType);
        poDstLayer->CreateGeomField(&oDstFieldDefn);
    }

    OGRErr eErr = OGRERR_NONE;

    for (auto &poSrcFeature : poSrcLayer)
    {
        const OGRGeometry *poSrcGeometry = poSrcFeature->GetGeometryRef();
        OGRwkbGeometryType eSrcFtrType = poSrcGeometry->getGeometryType();

        if (!IsGeomTypeSupported(eSrcFtrType))
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Unsupported geometry type '%s'.", OGRGeometryTypeToName(eSrcFtrType));
            eErr = OGRERR_UNSUPPORTED_GEOMETRY_TYPE;
            break;
        }

        if (IsGeomTypeMulti(eSrcFtrType))
        {
            const OGRGeometryCollection *poSrcGeometryCollection = poSrcGeometry->toGeometryCollection();
            for (auto &poSrcSingularGeometry : poSrcGeometryCollection)
            {
                eErr = CopyFeature(poDstLayer, poSrcFeature.get(), poSrcSingularGeometry);
                if (eErr != OGRERR_NONE)
                {
                    break;
                }
            }
        }
        else
        {
            eErr = CopyFeature(poDstLayer, poSrcFeature.get(), poSrcGeometry);
        }

        if (eErr != OGRERR_NONE)
        {
            break;
        }
    }

    return eErr;
}

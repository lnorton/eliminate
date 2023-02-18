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

#include <iostream>
#include <vector>
#include <list>
#include <unordered_set>
#include <algorithm>

#include "gdal.h"
#include "ogrsf_frmts.h"

#include "geos_c.h"

#include "eliminate.h"


extern OGRErr CopyFeature(OGRLayer *poDstLayer, const OGRFeature *poSrcFeature, const OGRGeometry *poGeometry);

class FeatureCreature
{
public:
    struct neighbor_t
    {
        FeatureCreature *poCreature;
        double dfBoundaryLength;
        void addCreatureToMerge(FeatureCreature *poCreatureToMerge)
        {
            poCreature->addCreatureToMerge(poCreatureToMerge);
        }
        static bool larger(const neighbor_t &a, const neighbor_t &b) { return a.poCreature->area() >= b.poCreature->area(); };
        static bool smaller(const neighbor_t &a, const neighbor_t &b) { return a.poCreature->area() < b.poCreature->area(); };
        static bool longer(const neighbor_t &a, const neighbor_t &b) { return a.dfBoundaryLength >= b.dfBoundaryLength; };
        typedef bool (*comp_t)(const neighbor_t &, const neighbor_t &);
    };

private:
    OGRFeatureUniquePtr m_poFeature;
    GEOSContextHandle_t m_hGEOSContext;
    GEOSGeometry* m_poGEOSGeometry;
    const GEOSPreparedGeometry *m_poGEOSPreparedGeometry;
    double m_dfArea;
    std::list<neighbor_t> m_lstNeighbors;
    std::list<FeatureCreature *> m_lstpoCreaturesToMerge;

public:
    FeatureCreature(OGRFeatureUniquePtr poFeature, GEOSContextHandle_t hGEOSCtxt) :
        m_poFeature(std::move(poFeature)), m_hGEOSContext(hGEOSCtxt),
        m_poGEOSGeometry(nullptr), m_poGEOSPreparedGeometry(nullptr),
        m_dfArea(-1.0)
    {
    }

    virtual ~FeatureCreature()
    {
        if (m_poGEOSPreparedGeometry != nullptr)
        {
            GEOSPreparedGeom_destroy_r(m_hGEOSContext ,m_poGEOSPreparedGeometry);
        }
        if (m_poGEOSGeometry != nullptr)
        {
            GEOSGeom_destroy_r(m_hGEOSContext, m_poGEOSGeometry);
        }
    }

    const OGRFeature *feature() const
    {
        return m_poFeature.get();
    }

    OGRErr initGeometry()
    {
        if (m_poGEOSGeometry != nullptr)
        {
            return OGRERR_NONE;
        }

        OGRGeometry *poGeom = m_poFeature->GetGeometryRef();
        if (poGeom == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No geometry?");
            return OGRERR_FAILURE;
        }

        m_poGEOSGeometry = poGeom->exportToGEOS(m_hGEOSContext);
        if (m_poGEOSGeometry == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "That shouldn't happen. (1)");
            return OGRERR_FAILURE;
        }

        return OGRERR_NONE;
    }

    GEOSGeometry *geometry()
    {
        if (m_poGEOSGeometry == nullptr)
        {
            initGeometry();
        }
        return m_poGEOSGeometry;
    }

    OGRErr initPreparedGeometry()
    {
        if (m_poGEOSPreparedGeometry != nullptr)
        {
            return OGRERR_NONE;
        }

        OGRErr eErr = initGeometry();
        if (eErr != OGRERR_NONE)
        {
            return eErr;
        }

        m_poGEOSPreparedGeometry = GEOSPrepare_r(m_hGEOSContext, m_poGEOSGeometry);
        if (m_poGEOSPreparedGeometry == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "That shouldn't happen. (2)");
            return OGRERR_FAILURE;
        }

        return OGRERR_NONE;
    }

    const GEOSPreparedGeometry *preparedGeometry()
    {
        if (m_poGEOSPreparedGeometry == nullptr)
        {
            initPreparedGeometry();
        }
        return m_poGEOSPreparedGeometry;
    }

    double area()
    {
        if (m_dfArea < 0.0)
        {
            if (1 != GEOSArea_r(m_hGEOSContext, geometry(), &m_dfArea))
            {
                CPLError(CE_Warning, CPLE_AppDefined, "Failed area calculation?");
                m_dfArea = 0.0;
            }
        }
        return m_dfArea;
    }

    void addNeighborIfTouching(FeatureCreature* poNeighbor)
    {
        // TODO: Can simply perform the intersection to determine if they touch, but is it more expensive?

        if (1 == GEOSPreparedTouches_r(m_hGEOSContext, preparedGeometry(), poNeighbor->geometry()))
        {
            GEOSGeometry *poIntersection = GEOSIntersection_r(m_hGEOSContext, geometry(),  poNeighbor->geometry());

            double dfLength = 0.0;
            if (1 == GEOSLength_r(m_hGEOSContext, poIntersection, &dfLength))
            {
                m_lstNeighbors.push_back({poNeighbor, dfLength});
            }
            else
            {
                char *type = GEOSGeomType_r(m_hGEOSContext, poIntersection);
                CPLError(CE_Warning, CPLE_AppDefined, "Failed length calculation on boundary of type %s.", type);
                GEOSFree_r(m_hGEOSContext, type);
                m_lstNeighbors.push_back({poNeighbor, 0.0});
            }

            GEOSGeom_destroy(poIntersection);
        }
    }

    neighbor_t *findNeighbor(neighbor_t::comp_t comp)
    {
        auto itr = std::min_element(m_lstNeighbors.begin(), m_lstNeighbors.end(), comp);
        return itr != m_lstNeighbors.end() ? &*itr : nullptr;
    }

    neighbor_t *findNeighbor(EliminateMergeType eMergeType)
    {
        switch (eMergeType)
        {
            default:
            case ELIMINATE_MERGE_LARGEST_AREA:
                return findNeighbor(neighbor_t::larger);

            case ELIMINATE_MERGE_SMALLEST_AREA:
                return findNeighbor(neighbor_t::smaller);

            case ELIMINATE_MERGE_LONGEST_BOUNDARY:
                return findNeighbor(neighbor_t::longer);
        }
    }

    void addCreatureToMerge(FeatureCreature *poCreature)
    {
        m_lstpoCreaturesToMerge.push_back(poCreature);
    }

    std::list<FeatureCreature *> allCreaturesToMerge() const
    {
        std::list<FeatureCreature *> lstpoAllCreaturesToMerge;
        for (auto poCreature : m_lstpoCreaturesToMerge)
        {
            lstpoAllCreaturesToMerge.push_back(poCreature);
            std::list<FeatureCreature *> lstpoCreaturesCreatures = poCreature->allCreaturesToMerge();
            lstpoAllCreaturesToMerge.insert(lstpoAllCreaturesToMerge.end(), lstpoCreaturesCreatures.begin(), lstpoCreaturesCreatures.end());
        }
        return lstpoAllCreaturesToMerge;
    }
};

EliminateOptions *EliminateOptionsNew()
{
    EliminateOptions *psOptions = new EliminateOptions;
    psOptions->pszSrcFilename = nullptr;
    psOptions->pszSrcLayerName = nullptr;
    psOptions->pszDstFilename = nullptr;
    psOptions->pszDstLayerName = nullptr;
    psOptions->pszFormat = nullptr;
    psOptions->pszWhere = nullptr;
    psOptions->eMergeType = ELIMINATE_MERGE_LARGEST_AREA;
    return psOptions;
}

void EliminateOptionsFree(EliminateOptions *psOptions)
{
    if (psOptions != nullptr)
    {
        CPLFree(psOptions->pszSrcFilename);
        CPLFree(psOptions->pszSrcLayerName);
        CPLFree(psOptions->pszDstFilename);
        CPLFree(psOptions->pszDstLayerName);
        CPLFree(psOptions->pszFormat);
        CPLFree(psOptions->pszWhere);
        delete psOptions;
    }
}

// Because zero is a valid FID, we can't trust CPLAtoGIntBig, since it uses
// atoll, which returns zero for all failures, nor CPLAtoGIntBigEx, since it
// only checks for overflow, and not whether the source string was parsable.
//
static GIntBig CPLAtoFID(const char *pszString)
{
    if (pszString == nullptr || *pszString == '\0')
    {
        return OGRNullFID;
    }
    char *pszFailure = nullptr;
    errno = 0;
    GIntBig nFID = strtoll(pszString, &pszFailure, 10);
    if (errno == ERANGE || nFID < 0 || (pszFailure != nullptr && *pszFailure != '\0'))
    {
        return OGRNullFID;
    }
    return nFID;
}

OGRErr EliminatePolygonsWithOptions(EliminateOptions *psOptions)
{
    OGRSFDriverH hDriver = OGRGetDriverByName(psOptions->pszFormat);
    if (hDriver == nullptr)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Unable to find format driver named %s.", psOptions->pszFormat);
        return OGRERR_FAILURE;
    }

    OGRErr eErr = OGRERR_FAILURE;

    int nFlags = GDAL_OF_VECTOR | GDAL_OF_READONLY | GDAL_OF_VERBOSE_ERROR;
    GDALDatasetH hSrcDS = GDALOpenEx(psOptions->pszSrcFilename, nFlags, nullptr, nullptr, nullptr);
    if (hSrcDS != nullptr)
    {
        GDALDatasetH hDstDS = reinterpret_cast<GDALDatasetH>(OGR_Dr_CreateDataSource(hDriver, psOptions->pszDstFilename, nullptr));
        if (hDstDS != nullptr)
        {
            eErr = EliminatePolygonsWithOptionsEx(hSrcDS, psOptions->pszSrcLayerName, hDstDS,  psOptions->pszDstLayerName, psOptions);
            GDALClose(hDstDS);
        }
        GDALClose(hSrcDS);
    }

    return eErr;
}

OGRErr EliminatePolygonsWithOptionsEx(GDALDatasetH hSrcDS, const char *pszSrcLayerName, GDALDatasetH hDstDS, const char *pszDstLayerName, EliminateOptions *psOptions)
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

    if (poDstLayer->GetLayerDefn()->GetGeomFieldCount() == 0)
    {
        for (int iField = 0, nCount = poSrcLayerDefn->GetGeomFieldCount(); iField < nCount; iField++)
        {
            OGRGeomFieldDefn *poSrcFieldDefn = poSrcLayerDefn->GetGeomFieldDefn(iField);
            poDstLayer->CreateGeomField(poSrcFieldDefn);
        }
    }

    if (psOptions->pszWhere != nullptr)
    {
        CPLString osWhere = psOptions->pszWhere;

        // There seems to be no way to force it into the OGRSQL dialect,
        // so kludge the where clause to keep it from blowing up.
        //
        CPLString osDriverName = poSrcDS->GetDriverName();
        if (osDriverName == "SQLite" || osDriverName == "GPKG")
        {
            CPLString osGeomColumn = poSrcLayer->GetGeometryColumn();
            if (!osGeomColumn.empty())
            {
                CPLString osAreaExpr = "ST_Area(";
                osAreaExpr += osGeomColumn;
                osAreaExpr += ")";
                osWhere.replaceAll("OGR_GEOM_AREA", osAreaExpr);
            }
        }

        return EliminatePolygonsByQuery(OGRLayer::ToHandle(poSrcLayer), OGRLayer::ToHandle(poDstLayer), psOptions->eMergeType, osWhere);
    }

    return OGRERR_UNSUPPORTED_OPERATION;
}

OGRErr EliminatePolygonsByQuery(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, const char *pszWhere)
{
    if (pszWhere == nullptr || strlen(pszWhere) == 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Filter must be specified.");
        return OGRERR_FAILURE;
    }

    OGRLayer *poSrcLayer = OGRLayer::FromHandle(hSrcLayer);

    // FIXME: This does not fail with bad WHERE statements.
    OGRErr eErr = poSrcLayer->SetAttributeFilter(pszWhere);

    if (eErr != OGRERR_NONE)
    {
        return eErr;
    }

    std::vector<GIntBig> vecFIDsToEliminate;
    for (auto &poFeature : poSrcLayer)
    {
        GIntBig nFID = poFeature->GetFID();
        vecFIDsToEliminate.push_back(nFID);
    }

    eErr = poSrcLayer->SetAttributeFilter(nullptr);

    if (eErr != OGRERR_NONE)
    {
        return eErr;
    }

    return EliminatePolygonsByFID(hSrcLayer, hDstLayer, eMergeType, vecFIDsToEliminate.data(), vecFIDsToEliminate.size());
}

OGRErr EliminatePolygonsByFIDStrList(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, char **papszEliminateFIDs)
{
    std::vector<GIntBig> vecFIDsToEliminate;
    for (int i = 0, n = CSLCount(papszEliminateFIDs); i < n; i++)
    {
        const char *pszFID = papszEliminateFIDs[i];
        GIntBig nFID =  CPLAtoFID(pszFID);
        vecFIDsToEliminate.push_back(nFID);
    }

    return EliminatePolygonsByFID(hSrcLayer, hDstLayer, eMergeType, vecFIDsToEliminate.data(), vecFIDsToEliminate.size());
}

OGRErr EliminatePolygonsByFID(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, EliminateMergeType eMergeType, GIntBig *panEliminateFIDs , int nCount)
{
    int nMajor, nMinor, nPatch;
    bool bHaveGEOS = OGRGetGEOSVersion(&nMajor, &nMinor, &nPatch);

    if (!bHaveGEOS)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Installed GDAL library does not support GEOS.");
        return OGRERR_UNSUPPORTED_OPERATION;
    }

    OGRLayer *poSrcLayer = OGRLayer::FromHandle(hSrcLayer);
    OGRLayer *poDstLayer = OGRLayer::FromHandle(hDstLayer);

    std::unordered_set<GIntBig> setFIDsToEliminate;
    for (int i = 0; i < nCount; i++)
    {
        GIntBig nFID = panEliminateFIDs[i];
        if (nFID >= 0)
        {
            setFIDsToEliminate.insert(nFID);
        }
    }

    GEOSContextHandle_t hGEOSCtxt = OGRGeometry::createGEOSContext();
    GEOSSTRtree *poSTRTree = GEOSSTRtree_create_r(hGEOSCtxt, 10);

    std::list<FeatureCreature> lstFeatures;
    std::list<FeatureCreature *> lstpoFeaturesToKeep;
    std::list<FeatureCreature *> lstpoFeaturesToEliminate;

    for(auto &poFeature : poSrcLayer)
    {
        lstFeatures.emplace_back(std::move(poFeature), hGEOSCtxt);
        FeatureCreature &creature = lstFeatures.back();

        OGRErr eErr = creature.initGeometry();
        if (eErr != OGRERR_NONE)
        {
            continue;
        }

        GIntBig nFID = creature.feature()->GetFID();
        auto itr = setFIDsToEliminate.find(nFID);
        if (itr != setFIDsToEliminate.end())
        {
            eErr = creature.initPreparedGeometry();
            if (eErr != OGRERR_NONE)
            {
                continue;
            }

            setFIDsToEliminate.erase(itr);
            lstpoFeaturesToEliminate.push_back(&creature);
        }
        else
        {
            lstpoFeaturesToKeep.push_back(&creature);
        }

        GEOSSTRtree_insert_r(hGEOSCtxt, poSTRTree, creature.geometry(), &creature);
    }

    if (!setFIDsToEliminate.empty())
    {
        CPLError(CE_Warning, CPLE_AppDefined, "%lu selected features not found in source layer!", setFIDsToEliminate.size());
    }

    for(auto poCreature : lstpoFeaturesToEliminate)
    {
        std::list<FeatureCreature *> lstpoNeighbors;

        struct capture_t
        {
            FeatureCreature *poCreature;
            std::list<FeatureCreature *> *plstpoNeighbors;
        };
        capture_t capture = {poCreature, &lstpoNeighbors};
        GEOSQueryCallback callback = [](void *poItem, void *poUserData) {
            auto poNeighbor = static_cast<FeatureCreature *>(poItem);
            auto poCapture = static_cast<capture_t *>(poUserData);
            if (poCapture->poCreature != poNeighbor)
            {
                poCapture->plstpoNeighbors->push_back(poNeighbor);
            }
        };

        GEOSSTRtree_query_r(hGEOSCtxt, poSTRTree, poCreature->geometry(), callback, &capture);

        if (lstpoNeighbors.size() == 0)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No neighbors?");
            continue;
        }

        for (auto poNeighbor : lstpoNeighbors)
        {
            poCreature->addNeighborIfTouching(poNeighbor);
        }

        FeatureCreature::neighbor_t *poNeighbor = poCreature->findNeighbor(eMergeType);

        if (poNeighbor == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No touching neighbors?");
            continue;
        }

        poNeighbor->addCreatureToMerge(poCreature);
    }

    const bool bUseGEOSGeometries = true;

    for(auto poCreature : lstpoFeaturesToKeep)
    {
        const OGRFeature *poFeature = poCreature->feature();
        const OGRGeometry *poGeometry = poFeature->GetGeometryRef();
        std::list<FeatureCreature *> lstpoCreaturesToMerge = poCreature->allCreaturesToMerge();
        OGRErr eErr;

        if (lstpoCreaturesToMerge.empty())
        {
            eErr = CopyFeature(poDstLayer, poFeature, poGeometry);
        }
        else
        {
            OGRGeometryUniquePtr poCombinedGeometry;
            if (bUseGEOSGeometries)
            {
                // Clone the geometries because the collection assumes ownership.
                std::vector<GEOSGeometry *> vecGeometries;
                vecGeometries.push_back(GEOSGeom_clone_r(hGEOSCtxt, poCreature->geometry()));
                for (auto poCreatureToMerge : lstpoCreaturesToMerge)
                {
                    vecGeometries.push_back(GEOSGeom_clone_r(hGEOSCtxt, poCreatureToMerge->geometry()));
                }
                GEOSGeometry *poGEOSGeometryCollection = GEOSGeom_createCollection_r(hGEOSCtxt, GEOS_MULTIPOLYGON, vecGeometries.data(), vecGeometries.size());
                GEOSGeometry *poGEOSCombinedGeometry = GEOSUnaryUnion_r(hGEOSCtxt, poGEOSGeometryCollection);
                poCombinedGeometry.reset(OGRGeometryFactory::createFromGEOS(hGEOSCtxt, poGEOSCombinedGeometry));
                poCombinedGeometry->assignSpatialReference(poGeometry->getSpatialReference());
                GEOSGeom_destroy_r(hGEOSCtxt, poGEOSCombinedGeometry);
                GEOSGeom_destroy_r(hGEOSCtxt, poGEOSGeometryCollection);
            }
            else
            {
                poCombinedGeometry.reset(poGeometry->clone());
                for (auto poCreatureToMerge : lstpoCreaturesToMerge)
                {
                    poCombinedGeometry.reset(poCombinedGeometry->Union(poCreatureToMerge->feature()->GetGeometryRef()));
                }
            }
            eErr = CopyFeature(poDstLayer, poFeature, poCombinedGeometry.get());
        }

        if (eErr != OGRERR_NONE)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "Failed to create feature in destination layer.");
        }
    }

    // Prior to GEOS 3.9, the tree does not copy the geometry, so it must be
    // destroyed before the feature nodes, and the context last of all.
    //
    GEOSSTRtree_destroy_r(hGEOSCtxt, poSTRTree);
    lstFeatures.clear();
    OGRGeometry::freeGEOSContext(hGEOSCtxt);

    return OGRERR_NONE;
}

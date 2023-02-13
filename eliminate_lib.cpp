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
    typedef std::pair<FeatureCreature *, double> neighbor_t;
    typedef bool (*neighbor_comp_t)(FeatureCreature::neighbor_t &, FeatureCreature::neighbor_t &);

protected:
    OGRFeatureUniquePtr m_poFeature;
    GEOSContextHandle_t m_hGEOSContext;
    GEOSGeometry* m_poGEOSGeometry;
    const GEOSPreparedGeometry *m_poGEOSPreparedGeometry;
    double m_dfArea;
    std::list<neighbor_t> m_listNeighbors;
    neighbor_t *m_poSmallestNeighbor, *m_poBiggestNeighbor, *m_poLongestNeighbor;
    std::list<FeatureCreature *> m_listNeighborsToMerge;

public:
    FeatureCreature(OGRFeatureUniquePtr poFeature, GEOSContextHandle_t hGEOSCtxt) :
        m_poFeature(std::move(poFeature)), m_hGEOSContext(hGEOSCtxt),
        m_poGEOSGeometry(nullptr), m_poGEOSPreparedGeometry(nullptr),
        m_dfArea(-1.0), m_poSmallestNeighbor(nullptr),
        m_poBiggestNeighbor(nullptr), m_poLongestNeighbor(nullptr)
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

        OGRGeometry *pGeom = m_poFeature->GetGeometryRef();
        if (pGeom == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No geometry?");
            return OGRERR_FAILURE;
        }

        m_poGEOSGeometry = pGeom->exportToGEOS(m_hGEOSContext);
        if (m_poGEOSGeometry == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "That shouldn't happen. (1)");
            return OGRERR_FAILURE;
        }

        return OGRERR_NONE;
    }

    GEOSGeometry *geometry()
    {
        initGeometry();
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
        initPreparedGeometry();
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

    void add_neighbor_if_touching(FeatureCreature* poNeighbor)
    {
        // TODO: Can simply perform the intersection to determine if they touch, but is it more expensive?

        if (1 == GEOSPreparedTouches_r(m_hGEOSContext, preparedGeometry(), poNeighbor->geometry()))
        {
            GEOSGeometry *poIntersection = GEOSIntersection_r(m_hGEOSContext, geometry(),  poNeighbor->geometry());

            double length = 0.0;
            if (1 == GEOSLength_r(m_hGEOSContext, poIntersection, &length))
            {
                m_listNeighbors.push_back(std::make_pair(poNeighbor, length));
            }
            else
            {
                char *type = GEOSGeomType_r(m_hGEOSContext, poIntersection);
                CPLError(CE_Warning, CPLE_AppDefined, "Failed length calculation on boundary of type %s.", type);
                GEOSFree_r(m_hGEOSContext, type);
                m_listNeighbors.push_back(std::make_pair(poNeighbor, 0.0));
            }

            GEOSGeom_destroy(poIntersection);
        }
    }

    neighbor_t *find_neighbor(neighbor_comp_t comp)
    {
        auto itr = std::max_element(m_listNeighbors.begin(), m_listNeighbors.end(), comp);
        return itr != m_listNeighbors.end() ? &*itr : nullptr;
    }

    neighbor_t *find_smallest_neighbor()
    {
        auto smaller = [](FeatureCreature::neighbor_t &a, FeatureCreature::neighbor_t &b) { return a.first->area() < b.first->area(); };
        m_poSmallestNeighbor = find_neighbor(smaller);
        return m_poSmallestNeighbor;
    }

    neighbor_t *find_biggest_neighbor()
    {
        auto bigger = [](FeatureCreature::neighbor_t &a, FeatureCreature::neighbor_t &b) { return a.first->area() >= b.first->area(); };
        m_poBiggestNeighbor = find_neighbor(bigger);
        return m_poBiggestNeighbor;
    }

    neighbor_t *find_longest_neighbor()
    {
        auto longer = [](FeatureCreature::neighbor_t &a, FeatureCreature::neighbor_t &b) { return a.second >= b.second; };
        m_poLongestNeighbor = find_neighbor(longer);
        return m_poLongestNeighbor;
    }

    void add_neighbor_to_merge(FeatureCreature *poNeighbor)
    {
        m_listNeighborsToMerge.push_back(poNeighbor);
    }

    std::list<FeatureCreature *> collectNeighborsToMerge() const
    {
        std::list<FeatureCreature *> listAllNeighborsToMerge;
        for (auto poNeighbor : m_listNeighborsToMerge)
        {
            listAllNeighborsToMerge.push_back(poNeighbor);
            std::list<FeatureCreature *> listNeighborsNeighbors = poNeighbor->collectNeighborsToMerge();
            for (auto poNeighborsNeighbor : listNeighborsNeighbors)
            {
                listAllNeighborsToMerge.push_back(poNeighborsNeighbor);
            }
        }
        return listAllNeighborsToMerge;
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
        GDALDatasetH hDstDS = reinterpret_cast<GDALDatasetH>(OGR_Dr_CreateDataSource(hDriver, psOptions->pszDstFilename, /* DSCO */ nullptr));
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

        // Sigh... there seems to be no way to force it into the OGRSQL dialect,
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

        return EliminatePolygonsByQuery(OGRLayer::ToHandle(poSrcLayer), OGRLayer::ToHandle(poDstLayer), osWhere);
    }

    return OGRERR_UNSUPPORTED_OPERATION;
}

OGRErr EliminatePolygonsByQuery(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, const char *pszWhere)
{
    if (pszWhere == nullptr || strlen(pszWhere) == 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Filter must be specified.");
        return OGRERR_FAILURE;
    }

    OGRLayer *poSrcLayer = OGRLayer::FromHandle(hSrcLayer);

    OGRErr eErr = poSrcLayer->SetAttributeFilter(pszWhere);

    if (eErr != OGRERR_NONE)
    {
        return eErr;
    }

    CPLStringList aosFIDs;

    for (auto &poFeature : poSrcLayer)
    {
        CPLString sFID = std::to_string(poFeature->GetFID());
        aosFIDs.AddString(sFID);
    }

    poSrcLayer->SetAttributeFilter(nullptr);

    eErr = EliminatePolygonsByFID(hSrcLayer, hDstLayer, aosFIDs);

    return eErr;
}

OGRErr EliminatePolygonsByFID(OGRLayerH hSrcLayer, OGRLayerH hDstLayer, char **papszEliminateFIDs)
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

    // We should eliminate the features in the order they're given, since it
    // may affect the selection of the feature each is merged with.
    //
    std::list<GIntBig> listFIDsToEliminate;
    std::unordered_set<GIntBig> setFIDsToEliminate;

    for (int i = 0, n = CSLCount(papszEliminateFIDs); i < n; i++)
    {
        const char *pszFID = papszEliminateFIDs[i];
        GIntBig nFID =  CPLAtoFID(pszFID);
        if (nFID != OGRNullFID && setFIDsToEliminate.find(nFID) == setFIDsToEliminate.end())
        {
            setFIDsToEliminate.insert(nFID);
            listFIDsToEliminate.push_back(nFID);
        }
    }

    GEOSContextHandle_t hGEOSCtxt = OGRGeometry::createGEOSContext();
    GEOSSTRtree *poSTRTree = GEOSSTRtree_create_r(hGEOSCtxt, 10);

    std::list<FeatureCreature> listFeatures;
    std::list<FeatureCreature *> listpoFeaturesToKeep;
    std::list<FeatureCreature *> listpoFeaturesToEliminate;

    for(auto &poFeature : poSrcLayer)
    {
        listFeatures.emplace_back(std::move(poFeature), hGEOSCtxt);
        FeatureCreature &creature = listFeatures.back();

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
            listpoFeaturesToEliminate.push_back(&creature);
        }
        else
        {
            listpoFeaturesToKeep.push_back(&creature);
        }

        GEOSSTRtree_insert_r(hGEOSCtxt, poSTRTree, creature.geometry(), &creature);
    }

    if (!setFIDsToEliminate.empty())
    {
        CPLError(CE_Warning, CPLE_AppDefined, "%lu selected features not found in source layer!", setFIDsToEliminate.size());
    }

    for(auto poCreature : listpoFeaturesToEliminate)
    {
        std::list<FeatureCreature *> neighbors;

        typedef std::pair<FeatureCreature *, std::list<FeatureCreature *> *> capture_t;
        capture_t capture = std::make_pair(poCreature, &neighbors);

        auto callback = [](void *poItem, void *poUserData) {
            auto poNeighbor = static_cast<FeatureCreature *>(poItem);
            auto poCapture = static_cast<capture_t *>(poUserData);
            if (poCapture->first != poNeighbor)
            {
                poCapture->second->push_back(poNeighbor);
            }
        };

        GEOSSTRtree_query_r(hGEOSCtxt, poSTRTree, poCreature->geometry(), callback, &capture);

        if (neighbors.size() == 0)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No neighbors?");
            continue;
        }

        for (auto poNeighbor : neighbors)
        {
            poCreature->add_neighbor_if_touching(poNeighbor);
        }

        FeatureCreature::neighbor_t *poNeighbor = poCreature->find_longest_neighbor();

        if (poNeighbor == nullptr)
        {
            CPLError(CE_Warning, CPLE_AppDefined, "No touching neighbors?");
            continue;
        }

        poNeighbor->first->add_neighbor_to_merge(poCreature);
    }

    for(auto poCreature : listpoFeaturesToKeep)
    {
        std::list<FeatureCreature *> listNeighborsToMerge = poCreature->collectNeighborsToMerge();
        const OGRFeature *poFeature = poCreature->feature();
        OGRGeometryUniquePtr poCombinedGeometry(poFeature->GetGeometryRef()->clone());

        if (!listNeighborsToMerge.empty())
        {
#if 0
            // This is considerably slower.
            for (auto poNeighbor : listNeighborsToMerge)
            {
                poCombinedGeometry.reset(poCombinedGeometry->Union(poNeighbor->feature()->GetGeometryRef()));
            }
#endif

            // We clone the geometries because the collection assumes ownership.
            std::vector<GEOSGeometry *> vecGeometries;
            vecGeometries.push_back(GEOSGeom_clone_r(hGEOSCtxt, poCreature->geometry()));
            for (auto poNeighbor : listNeighborsToMerge)
            {
                vecGeometries.push_back(GEOSGeom_clone_r(hGEOSCtxt, poNeighbor->geometry()));
            }
            GEOSGeometry *poGEOSGeometryCollection = GEOSGeom_createCollection_r(hGEOSCtxt, GEOS_MULTIPOLYGON, vecGeometries.data(), vecGeometries.size());
            GEOSGeometry *poGEOSCombinedGeometry = GEOSUnaryUnion_r(hGEOSCtxt, poGEOSGeometryCollection);
            poCombinedGeometry.reset(OGRGeometryFactory::createFromGEOS(hGEOSCtxt, poGEOSCombinedGeometry));
            poCombinedGeometry->assignSpatialReference(poFeature->GetGeometryRef()->getSpatialReference());
            GEOSGeom_destroy_r(hGEOSCtxt, poGEOSCombinedGeometry);
            GEOSGeom_destroy_r(hGEOSCtxt, poGEOSGeometryCollection);
        }

        OGRErr eErr = CopyFeature(poDstLayer, poFeature, poCombinedGeometry.get());

        if (eErr != OGRERR_NONE)
        {
            break;
        }
    }

    // Prior to GEOS 3.9, the tree does not copy the geometry, so it must be
    // destroyed before the feature nodes, and the context last of all.
    //
    GEOSSTRtree_destroy_r(hGEOSCtxt, poSTRTree);
    listFeatures.clear();
    OGRGeometry::freeGEOSContext(hGEOSCtxt);

    return OGRERR_NONE;
}

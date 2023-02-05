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
#include <algorithm>

#include "gdal.h"
#include "ogrsf_frmts.h"

#include "geos_c.h"

#include "eliminate.h"


OGRErr EliminatePolygons(GDALDatasetH hSrcDS, const char* pszSrcLayerName, GDALDatasetH hDstDS, const char* pszDstLayerName)
{
    const double min_area = 0.0005;

    GDALDataset *pSrcDS = GDALDataset::FromHandle(hSrcDS);
    GDALDataset *pDstDS = GDALDataset::FromHandle(hDstDS);

    for (const auto &pLayer : pSrcDS->GetLayers())
    {
        OGRFeatureDefn *pLayerDefn = pLayer->GetLayerDefn();
        const char *layer_name = pLayerDefn->GetName();
        std::cout << "processing layer " << layer_name << std::endl;

        // Fields iterator not until available until GDAL 3.7
        for (int iField = 0, nCount = pLayerDefn->GetFieldCount(); iField < nCount; ++iField)
        {
            OGRFieldDefn *pFieldDefn = pLayerDefn->GetFieldDefn(iField);
            std::cout << "processing field '" << pFieldDefn->GetNameRef() << "': "
                      << pFieldDefn->GetFieldTypeName(pFieldDefn->GetType()) << std::endl;
        }

        for (int iField = 0, nCount = pLayerDefn->GetGeomFieldCount(); iField < nCount; iField++)
        {
            OGRGeomFieldDefn *pFieldDefn = pLayerDefn->GetGeomFieldDefn(iField);
            std::cout << "processing geom field '" << pFieldDefn->GetNameRef() << "': "
                      << OGRGeometryTypeToName(pFieldDefn->GetType()) << std::endl;
        }

        class FeatureCreature
        {
        private:
            OGRFeatureUniquePtr m_pFeature;
            GEOSContextHandle_t m_hGEOSContext;
            GEOSGeometry* m_pGEOSGeometry;
            const GEOSPreparedGeometry *m_pGEOSPreparedGeometry;
            double m_area;
            bool m_bAreaInitialized;
            typedef std::pair<FeatureCreature *, double> neighbor_t;
            std::list<neighbor_t> m_neighbors;
            neighbor_t *m_pBiggestNeighbor, *m_pLongestNeighbor;

        public:
            FeatureCreature(OGRFeatureUniquePtr pFeature, GEOSContextHandle_t hGEOSCtxt) :
                m_pFeature(std::move(pFeature)), m_hGEOSContext(hGEOSCtxt),
                m_pGEOSGeometry(nullptr), m_pGEOSPreparedGeometry(nullptr),
                m_area(0.0), m_bAreaInitialized(false),
                m_pBiggestNeighbor(nullptr), m_pLongestNeighbor(nullptr)
            {
            }

            virtual ~FeatureCreature()
            {
                if (m_pGEOSPreparedGeometry != nullptr)
                {
                    GEOSPreparedGeom_destroy_r(m_hGEOSContext ,m_pGEOSPreparedGeometry);
                }
                if (m_pGEOSGeometry != nullptr)
                {
                    GEOSGeom_destroy_r(m_hGEOSContext, m_pGEOSGeometry);
                }
            }

            GEOSGeometry *geometry()
            {
                if (m_pGEOSGeometry == nullptr)
                {
                    OGRGeometry *pGeom = m_pFeature->GetGeometryRef();
                    if (pGeom == nullptr)
                    {
                        CPLError(CE_Warning, CPLE_AppDefined, "No geometry?");
                    }
                    else
                    {
                        m_pGEOSGeometry = pGeom->exportToGEOS(m_hGEOSContext);
                        if (m_pGEOSGeometry == nullptr)
                        {
                            CPLError(CE_Warning, CPLE_AppDefined, "That shouldn't happen. (1)");
                        }
                    }
                }
                return m_pGEOSGeometry;
            }

            const GEOSPreparedGeometry *preparedGeometry()
            {
                if (m_pGEOSPreparedGeometry == nullptr)
                {
                    GEOSGeometry *pGeometry = this->geometry();
                    if (pGeometry != nullptr)
                    {
                        m_pGEOSPreparedGeometry = GEOSPrepare_r(m_hGEOSContext, pGeometry);
                        if (m_pGEOSPreparedGeometry == nullptr)
                        {
                            CPLError(CE_Warning, CPLE_AppDefined, "That shouldn't happen. (2)");
                        }
                    }
                }
                return m_pGEOSPreparedGeometry;
            }

            double area()
            {
                if (!m_bAreaInitialized)
                {
                    if (1 != GEOSArea_r(m_hGEOSContext, geometry(), &m_area))
                    {
                        CPLError(CE_Warning, CPLE_AppDefined, "Failed area calculation?");
                    }
                    m_bAreaInitialized = true;
                }
                return m_area;
            }

            void add_neighbor_if_touching(FeatureCreature* pNeighbor)
            {
                if (1 == GEOSPreparedTouches_r(m_hGEOSContext, preparedGeometry(), pNeighbor->geometry()))
                {
                    GEOSGeometry *pIntersection = GEOSIntersection_r(m_hGEOSContext, geometry(),  pNeighbor->geometry());

                    double length = 0.0;
                    if (1 == GEOSLength_r(m_hGEOSContext, pIntersection, &length))
                    {
                        m_neighbors.push_back(std::make_pair(pNeighbor, length));
                    }
                    else
                    {
                        char *type = GEOSGeomType_r(m_hGEOSContext, pIntersection);
                        CPLError(CE_Warning, CPLE_AppDefined, "Failed length calculation on boundary of type %s.", type);
                        GEOSFree_r(m_hGEOSContext, type);
                        m_neighbors.push_back(std::make_pair(pNeighbor, 0.0));
                    }

                    GEOSGeom_destroy(pIntersection);
                }
            }

            void find_biggest_neighbor()
            {
                auto bigger = [](FeatureCreature::neighbor_t &a, FeatureCreature::neighbor_t &b) { return a.first->area() > b.first->area(); };
                auto bigger_itr = std::max_element(m_neighbors.begin(), m_neighbors.end(), bigger);

                if (bigger_itr != m_neighbors.end())
                {
                    m_pBiggestNeighbor = &*bigger_itr;
                }
            }

            void find_longest_neighbor()
            {
                auto longer = [](FeatureCreature::neighbor_t &a, FeatureCreature::neighbor_t &b) { return a.second > b.second; };
                auto longer_itr = std::max_element(m_neighbors.begin(), m_neighbors.end(), longer);

                if (longer_itr != m_neighbors.end())
                {
                    m_pLongestNeighbor = &*longer_itr;
                }
            }

            std::list<neighbor_t> &neighbors()
            {
                return m_neighbors;
            }

            const std::list<neighbor_t> &neighbors() const
            {
                return m_neighbors;
            }
        };

        GEOSContextHandle_t hGEOSCtxt = OGRGeometry::createGEOSContext();
        GEOSSTRtree *pSTRTree = GEOSSTRtree_create_r(hGEOSCtxt, 10);

        std::list<FeatureCreature> features;

        for(auto &pFeature : pLayer)
        {
            features.emplace_back(std::move(pFeature), hGEOSCtxt);
            auto &creature = features.back();
            GEOSSTRtree_insert_r(hGEOSCtxt, pSTRTree, creature.geometry(), &creature);
        }

        for(auto &creature : features)
        {
            std::list<FeatureCreature *> neighbors;

            typedef std::pair<FeatureCreature *, std::list<FeatureCreature *> *> capture_t;
            capture_t capture = std::make_pair(&creature, &neighbors);

            auto callback = [](void *pItem, void *pUserData) {
                auto pNeighbor = static_cast<FeatureCreature *>(pItem);
                auto pCapture = static_cast<capture_t *>(pUserData);
                if (pCapture->first != pNeighbor)
                {
                    pCapture->second->push_back(pNeighbor);
                }
            };

            GEOSSTRtree_query_r(hGEOSCtxt, pSTRTree, creature.geometry(), callback, &capture);

            if (neighbors.size() == 0)
            {
                CPLError(CE_Warning, CPLE_AppDefined, "No neighbors?");
            }

            for (auto &pNeighbor : neighbors)
            {
                creature.add_neighbor_if_touching(pNeighbor);
            }

            if (creature.neighbors().size() == 0)
            {
                CPLError(CE_Warning, CPLE_AppDefined, "No touching neighbors?");
            }

            creature.find_biggest_neighbor();
            creature.find_longest_neighbor();
        }

        for(auto &creature : features)
        {
        }

        features.clear();
        GEOSSTRtree_destroy_r(hGEOSCtxt, pSTRTree);
        OGRGeometry::freeGEOSContext(hGEOSCtxt);

        std::cout << "Done." << std::endl;
    }

    return OGRERR_NONE;
}

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
#include <cstdlib>

#include "gdal.h"
#include "commonutils.h"
#include "eliminate.h"


static void PrintUsage(const char *pszErrorMessage = nullptr)
{
    std::cerr << "eliminate [-min <min_area> | -where <filter>] [-f <formatname>] <src_filename> <dst_filename>" << std::endl;
    if (pszErrorMessage != nullptr)
    {
        std::cerr << "FAILURE: " << pszErrorMessage << std::endl;
    }
}

static bool HasEnoughAdditionalArgs(char **papszArgv, int i, int nArgc, int nExtraArgs)
{
    if (i + nExtraArgs >= nArgc)
    {
        PrintUsage(CPLSPrintf("%s option requires %d argument(s)", papszArgv[i], nExtraArgs));
        return false;
    }
    else
    {
        return true;
    }
}

static OGRErr EliminatePolygonsCmdLineProcessor(int nArgc, char **papszArgv, EliminateOptions *psOptions)
{
    const char *pszSrcFilename = nullptr;
    const char *pszDstFilename = nullptr;
    const char *pszFormat = nullptr;
    const char *pszWhere = nullptr;
    const char *pszMin = nullptr;

    for (int i = 1; i < nArgc; ++i)
    {
        if (EQUAL(papszArgv[i], "-f"))
        {
            if (!HasEnoughAdditionalArgs(papszArgv, i, nArgc, 1))
            {
                return OGRERR_FAILURE;
            }
            pszFormat = papszArgv[++i];
        }
        else if (EQUAL(papszArgv[i], "-where"))
        {
            if (!HasEnoughAdditionalArgs(papszArgv, i, nArgc, 1))
            {
                return OGRERR_FAILURE;
            }
            pszWhere = papszArgv[++i];
        }
        else if (EQUAL(papszArgv[i], "-min"))
        {
            if (!HasEnoughAdditionalArgs(papszArgv, i, nArgc, 1))
            {
                return OGRERR_FAILURE;
            }
            pszMin = papszArgv[++i];
        }
        else if (pszSrcFilename == nullptr)
        {
            pszSrcFilename = papszArgv[i];
        }
        else if (pszDstFilename == nullptr)
        {
            pszDstFilename = papszArgv[i];
        }
        else
        {
            PrintUsage("Too many command options.");
            return OGRERR_FAILURE;
        }
    }

    if (pszSrcFilename != nullptr)
    {
        psOptions->pszSrcFilename = CPLStrdup(pszSrcFilename);
    }
    else
    {
        PrintUsage("Missing source filename.");
        return OGRERR_FAILURE;
    }

    if (pszDstFilename != nullptr)
    {
        psOptions->pszDstFilename = CPLStrdup(pszDstFilename);
    }
    else
    {
        PrintUsage("Missing destination filename.");
        return OGRERR_FAILURE;
    }

    if (pszWhere != nullptr && pszMin != nullptr)
    {
        PrintUsage("Cannot use '-min' with '-where'.");
        return OGRERR_FAILURE;
    }

    if (pszWhere == nullptr && pszMin == nullptr)
    {
        PrintUsage("Must specify '-min' or '-where'.");
        return OGRERR_FAILURE;
    }

    if (pszFormat != nullptr)
    {
        psOptions->pszFormat = CPLStrdup(pszFormat);
    }
    else
    {
        std::vector<CPLString> aoDrivers = GetOutputDriversFor(pszDstFilename, GDAL_OF_VECTOR);
        if (aoDrivers.empty())
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Cannot guess driver for %s", pszDstFilename);
            return OGRERR_FAILURE;
        }
        else
        {
            if (aoDrivers.size() > 1)
            {
                CPLError(CE_Warning, CPLE_AppDefined,
                         "Several drivers matching %s extension. Using %s",
                         CPLGetExtension(pszDstFilename), aoDrivers[0].c_str());
            }
            psOptions->pszFormat = CPLStrdup(aoDrivers[0].c_str());
        }
    }

    if (pszMin != nullptr)
    {
        double dfMin = CPLAtofM(pszMin);
        if (dfMin <= 0.0)
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Invalid value for -min: %s", pszMin);
            return OGRERR_FAILURE;
        }
        CPLString osWhere = CPLOPrintf("OGR_GEOM_AREA < %f", dfMin);
        psOptions->pszWhere = CPLStrdup(osWhere.c_str());
    }
    else
    {
        psOptions->pszWhere = CPLStrdup(pszWhere);
    }

    return OGRERR_NONE;
}

MAIN_START(argc, argv)
{
    GDALAllRegister();

    int nExitStatus = EXIT_FAILURE;

    char **papszArgv = argv;
    int nArgc = GDALGeneralCmdLineProcessor(argc, &papszArgv, 0);

    if (nArgc <= 0)
    {
        PrintUsage();
    }
    else
    {
        EliminateOptions *psOptions = EliminateOptionsNew();
        OGRErr eErr = EliminatePolygonsCmdLineProcessor(nArgc, papszArgv, psOptions);
        if (eErr == OGRERR_NONE)
        {
            eErr = EliminatePolygonsWithOptions(psOptions);
            if (eErr == OGRERR_NONE)
            {
                nExitStatus = EXIT_SUCCESS;
            }
        }
        EliminateOptionsFree(psOptions);
    }

    if (papszArgv != argv) {
        CSLDestroy(papszArgv);
    }

    GDALDestroy();

    return nExitStatus;
}

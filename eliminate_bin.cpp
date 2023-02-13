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


static int EliminatePolygonsCmdLineProcessor(int nArgc, char **papszArgv, EliminateOptions *psOptions)
{
    const char *pszSrcFilename = nullptr;
    const char *pszDstFilename = nullptr;
    const char *pszFormat = nullptr;
    char **papszDSCO = nullptr;

    for (int i = 1; i < nArgc; ++i)
    {
        if (pszSrcFilename == nullptr)
        {
            pszSrcFilename = papszArgv[i];
        }
        else if (pszDstFilename == nullptr)
        {
            pszDstFilename = papszArgv[i];
        }
    }

    if (pszSrcFilename != nullptr && pszDstFilename != nullptr)
    {
        psOptions->pszSrcFilename = CPLStrdup(pszSrcFilename);
        psOptions->pszDstFilename = CPLStrdup(pszDstFilename);
    }
    else
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Forget something?");
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

    psOptions->pszWhere = CPLStrdup("OGR_GEOM_AREA < 0.005");

    return EXIT_SUCCESS;
}

static void PrintUsage(const char *pszErrorMessage = nullptr)
{
    std::cerr << "eliminate <src_filename> <dst_filename>" << std::endl;
    if (pszErrorMessage != nullptr)
    {
        std::cerr << "FAILURE: " << pszErrorMessage << std::endl;
    }
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
        nExitStatus = EliminatePolygonsCmdLineProcessor(nArgc, papszArgv, psOptions);
        if (nExitStatus != EXIT_SUCCESS)
        {
            PrintUsage();
        }
        else
        {
            OGRErr eErr = EliminatePolygonsWithOptions(psOptions);
            nExitStatus = eErr == OGRERR_NONE ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        EliminateOptionsFree(psOptions);
    }

    if (papszArgv != argv) {
        CSLDestroy(papszArgv);
    }

    GDALDestroy();

    return nExitStatus;
}

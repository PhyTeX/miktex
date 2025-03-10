//========================================================================
//
// pdfimages.cc
//
// Copyright 1998-2003 Glyph & Cog, LLC
//
// Modified for Debian by Hamish Moffatt, 22 May 2002.
//
//========================================================================

//========================================================================
//
// Modified under the Poppler project - http://poppler.freedesktop.org
//
// All changes made under the Poppler project to this file are licensed
// under GPL version 2 or later
//
// Copyright (C) 2007-2008, 2010, 2018, 2022 Albert Astals Cid <aacid@kde.org>
// Copyright (C) 2010 Hib Eris <hib@hiberis.nl>
// Copyright (C) 2010 Jakob Voss <jakob.voss@gbv.de>
// Copyright (C) 2012, 2013, 2017 Adrian Johnson <ajohnson@redneon.com>
// Copyright (C) 2013 Suzuki Toshiya <mpsuzuki@hiroshima-u.ac.jp>
// Copyright (C) 2018 Adam Reichold <adam.reichold@t-online.de>
// Copyright (C) 2019, 2021 Oliver Sander <oliver.sander@tu-dresden.de>
// Copyright (C) 2019 Hartmut Goebel <h.goebel@crazy-compilers.com>
//
// To see a description of the changes please see the Changelog file that
// came with your tarball or type make ChangeLog if you are building from git
//
//========================================================================

#include "config.h"
#include <poppler-config.h>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include "parseargs.h"
#include "goo/GooString.h"
#include "goo/gmem.h"
#include "GlobalParams.h"
#include "Object.h"
#include "Stream.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "PDFDoc.h"
#include "PDFDocFactory.h"
#include "ImageOutputDev.h"
#include "Error.h"
#include "Win32Console.h"

static int firstPage = 1;
static int lastPage = 0;
static bool listImages = false;
static bool enablePNG = false;
static bool enableTiff = false;
static bool dumpJPEG = false;
static bool dumpJP2 = false;
static bool dumpJBIG2 = false;
static bool dumpCCITT = false;
static bool allFormats = false;
static bool pageNames = false;
static char ownerPassword[33] = "\001";
static char userPassword[33] = "\001";
static bool quiet = false;
static bool printVersion = false;
static bool printHelp = false;

static const ArgDesc argDesc[] = { { "-f", argInt, &firstPage, 0, "first page to convert" },
                                   { "-l", argInt, &lastPage, 0, "last page to convert" },
#ifdef ENABLE_LIBPNG
                                   { "-png", argFlag, &enablePNG, 0, "change the default output format to PNG" },
#endif
#ifdef ENABLE_LIBTIFF
                                   { "-tiff", argFlag, &enableTiff, 0, "change the default output format to TIFF" },
#endif
                                   { "-j", argFlag, &dumpJPEG, 0, "write JPEG images as JPEG files" },
                                   { "-jp2", argFlag, &dumpJP2, 0, "write JPEG2000 images as JP2 files" },
                                   { "-jbig2", argFlag, &dumpJBIG2, 0, "write JBIG2 images as JBIG2 files" },
                                   { "-ccitt", argFlag, &dumpCCITT, 0, "write CCITT images as CCITT files" },
                                   { "-all", argFlag, &allFormats, 0, "equivalent to -png -tiff -j -jp2 -jbig2 -ccitt" },
                                   { "-list", argFlag, &listImages, 0, "print list of images instead of saving" },
                                   { "-opw", argString, ownerPassword, sizeof(ownerPassword), "owner password (for encrypted files)" },
                                   { "-upw", argString, userPassword, sizeof(userPassword), "user password (for encrypted files)" },
                                   { "-p", argFlag, &pageNames, 0, "include page numbers in output file names" },
                                   { "-q", argFlag, &quiet, 0, "don't print any messages or errors" },
                                   { "-v", argFlag, &printVersion, 0, "print copyright and version info" },
                                   { "-h", argFlag, &printHelp, 0, "print usage information" },
                                   { "-help", argFlag, &printHelp, 0, "print usage information" },
                                   { "--help", argFlag, &printHelp, 0, "print usage information" },
                                   { "-?", argFlag, &printHelp, 0, "print usage information" },
                                   {} };

#if defined(MIKTEX)
int Main(int argc, char** argv)
#else
int main(int argc, char *argv[])
#endif
{
    GooString *fileName;
    char *imgRoot = nullptr;
    std::optional<GooString> ownerPW, userPW;
    ImageOutputDev *imgOut;
    bool ok;

    Win32Console win32Console(&argc, &argv);

    // parse args
    ok = parseArgs(argDesc, &argc, argv);
    if (!ok || (listImages && argc != 2) || (!listImages && argc != 3) || printVersion || printHelp) {
        fprintf(stderr, "pdfimages version %s\n", PACKAGE_VERSION);
        fprintf(stderr, "%s\n", popplerCopyright);
        fprintf(stderr, "%s\n", xpdfCopyright);
        if (!printVersion) {
            printUsage("pdfimages", "<PDF-file> <image-root>", argDesc);
        }
        if (printVersion || printHelp) {
            return 0;
        }
        return 99;
    }
    fileName = new GooString(argv[1]);
    if (!listImages) {
        imgRoot = argv[2];
    }

    // read config file
    globalParams = std::make_unique<GlobalParams>();
    if (quiet) {
        globalParams->setErrQuiet(quiet);
    }

    // open PDF file
    if (ownerPassword[0] != '\001') {
        ownerPW = GooString(ownerPassword);
    }
    if (userPassword[0] != '\001') {
        userPW = GooString(userPassword);
    }
    if (fileName->cmp("-") == 0) {
        delete fileName;
        fileName = new GooString("fd://0");
    }

    std::unique_ptr<PDFDoc> doc = PDFDocFactory().createPDFDoc(*fileName, ownerPW, userPW);
    delete fileName;

    if (!doc->isOk()) {
        return 1;
    }

    // check for copy permission
#ifdef ENFORCE_PERMISSIONS
    if (!doc->okToCopy()) {
        error(errNotAllowed, -1, "Copying of images from this document is not allowed.");
        return 3;
    }
#endif

    // get page range
    if (firstPage < 1) {
        firstPage = 1;
    }
    if (firstPage > doc->getNumPages()) {
        error(errCommandLine, -1, "Wrong page range given: the first page ({0:d}) can not be larger then the number of pages in the document ({1:d}).", firstPage, doc->getNumPages());
        return 99;
    }
    if (lastPage < 1 || lastPage > doc->getNumPages()) {
        lastPage = doc->getNumPages();
    }
    if (lastPage < firstPage) {
        error(errCommandLine, -1, "Wrong page range given: the first page ({0:d}) can not be after the last page ({1:d}).", firstPage, lastPage);
        return 99;
    }

    // write image files
    imgOut = new ImageOutputDev(imgRoot, pageNames, listImages);
    if (imgOut->isOk()) {
        if (allFormats) {
            imgOut->enablePNG(true);
            imgOut->enableTiff(true);
            imgOut->enableJpeg(true);
            imgOut->enableJpeg2000(true);
            imgOut->enableJBig2(true);
            imgOut->enableCCITT(true);
        } else {
            imgOut->enablePNG(enablePNG);
            imgOut->enableTiff(enableTiff);
            imgOut->enableJpeg(dumpJPEG);
            imgOut->enableJpeg2000(dumpJP2);
            imgOut->enableJBig2(dumpJBIG2);
            imgOut->enableCCITT(dumpCCITT);
        }
        doc->displayPages(imgOut, firstPage, lastPage, 72, 72, 0, true, false, false);
    }
    delete imgOut;

    return 0;
}

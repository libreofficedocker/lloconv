/* convert.cc - Convert documents using LibreOfficeKit
 *
 * Copyright (C) 2014-2022 Olly Betts
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <config.h>

#include "convert.h"

#include <climits>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sysexits.h>

#include <LibreOfficeKit/LibreOfficeKit.hxx>

#include "urlencode.h"

using namespace std;
using namespace lok;

// Install location for Debian packages (also Fedora on 32-bit architectures):
#define LO_PATH_DEBIAN "/usr/lib/libreoffice/program"

// Install location for Fedora packages on 64-bit architectures:
#define LO_PATH_FEDORA64 "/usr/lib64/libreoffice/program"

// Install location on macOS.  May not actually work there currently though,
// see: https://gitlab.com/ojwb/lloconv/-/issues/11
#define LO_PATH_MACOS "/Applications/LibreOffice.app/Contents/Frameworks"

const char * program = "<program>";

// Find a LibreOffice installation to use.
static const char *
get_lo_path()
{
    const char * lo_path = getenv("LO_PATH");
    if (lo_path) return lo_path;

    struct stat sb;
#define CHECK_DIR(P) if (stat(P"/versionrc", &sb) == 0 && S_ISREG(sb.st_mode)) return P
#ifdef __APPLE__
    CHECK_DIR(LO_PATH_MACOS);
#else
    CHECK_DIR(LO_PATH_DEBIAN);
    if (sizeof(void*) > 4) {
	CHECK_DIR(LO_PATH_FEDORA64);
    }
#endif

    // Check install locations for .deb files from libreoffice.org,
    // e.g. /opt/libreoffice6.3/program
    DIR* opt = opendir("/opt");
    if (opt) {
	// We require at least LibreOffice 4.3.
	unsigned long best_major = 4;
	unsigned long best_minor = 2;
	static string best_rc;
	struct dirent* d;
	while ((d = readdir(opt))) {
#ifdef DT_DIR
	    // Opportunistically skip non-directories if we can spot them
	    // just by looking at d_type.
	    if (d->d_type != DT_DIR && d->d_type != DT_UNKNOWN) {
		continue;
	    }
#endif
	    if (memcmp(d->d_name, "libreoffice", strlen("libreoffice")) != 0) {
		continue;
	    }

	    char* p = d->d_name + strlen("libreoffice");
	    unsigned long major = strtoul(p, &p, 10);
	    if (major == ULONG_MAX) continue;
	    unsigned long minor = 0;
	    if (*p == '.') {
		minor = strtoul(p + 1, &p, 10);
		if (minor == ULONG_MAX) continue;

		string rc = "/opt/";
		rc += d->d_name;
		rc += "/program";
		if (stat((rc + "/versionrc").c_str(), &sb) != 0 || !S_ISREG(sb.st_mode)) {
		    continue;
		}

		if (major > best_major ||
		    (major == best_major && minor > best_minor)) {
		    best_major = major;
		    best_minor = minor;
		    best_rc = std::move(rc);
		}
	    }
	}
	closedir(opt);
	if (!best_rc.empty()) {
	    return best_rc.c_str();
	}
    }

    cerr << program << ": LibreOffice install not found\n"
	"Set LO_PATH in the environment to the 'program' directory - e.g.:\n"
	"LO_PATH=/opt/libreoffice/program\n"
	"export LO_PATH\n";
    _Exit(1);
}

void *
convert_init()
{
    Office * llo = NULL;
    try {
	const char * lo_path = get_lo_path();
	llo = lok_cpp_init(lo_path);
	if (!llo) {
	    cerr << program << ": Failed to initialise LibreOfficeKit\n";
	    return NULL;
	}
	return static_cast<void*>(llo);
    } catch (const exception & e) {
	delete llo;
	cerr << program << ": LibreOfficeKit threw exception (" << e.what() << ")\n";
	return NULL;
    }
}

void
convert_cleanup(void * h_void)
{
    Office * llo = static_cast<Office *>(h_void);
    delete llo;
}

int
convert(void * h_void, bool url,
	const char * input, const char * output,
	const char * format, const char * options)
try {
    if (!h_void) return 1;
    Office * llo = static_cast<Office *>(h_void);

    string input_url;
    if (url) {
	input_url = input;
    } else {
	url_encode_path(input_url, input);
    }
    Document * lodoc = llo->documentLoad(input_url.c_str(), options);
    if (!lodoc) {
	const char * errmsg = llo->getError();
	cerr << program << ": LibreOfficeKit failed to load document (" << errmsg << ")\n";
	return 1;
    }

    string output_url;
    url_encode_path(output_url, output);
    if (!lodoc->saveAs(output_url.c_str(), format, options)) {
	const char * errmsg = llo->getError();
	cerr << program << ": LibreOfficeKit failed to export (" << errmsg << ")\n";
	delete lodoc;
	return 1;
    }

    delete lodoc;

    return 0;
} catch (const exception & e) {
    cerr << program << ": LibreOfficeKit threw exception (" << e.what() << ")\n";
    return 1;
}

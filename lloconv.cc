/* lloconv.cc - Convert a document using LibreOfficeKit
 *
 * Copyright (C) 2014,2015,2016,2018 Olly Betts
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <cstdlib>
#include <cstring>
#include <iostream>

#include <sysexits.h>

#include "convert.h"

#define PACKAGE_VERSION "6.1.0"

using namespace std;

static void
usage(ostream& os)
{
    os << "Usage: " << program << " [-u] [-f OUTPUT_FORMAT] [-o OPTIONS] INPUT_FILE OUTPUT_FILE\n\n";
    os << "  -u  INPUT_FILE is a URL\n";
    os << "Specifying options requires LibreOffice >= 4.3.0rc1\n\n";
    os << "Known values for OUTPUT_FORMAT include:\n";
    os << "  For text documents: doc docx fodt html odt ott pdf txt xhtml\n\n";
    os << "Known OPTIONS include: SkipImages\n";
    os << flush;
}

int
main(int argc, char **argv)
{
    program = argv[0];

    const char * format = NULL;
    const char * options = NULL;
    bool url = false;
    // FIXME: Use getopt() or something.
    ++argv;
    --argc;
    while (argv[0] && argv[0][0] == '-') {
	switch (argv[0][1]) {
	    case '-':
		if (argv[0][2] == '\0') {
		    // End of options.
		    ++argv;
		    --argc;
		    goto last_option;
		}
		if (strcmp(argv[0] + 2, "help") == 0) {
		    usage(cout);
		    exit(0);
		}
		if (strcmp(argv[0] + 2, "version") == 0) {
		    cout << "lloconv - lloconv " PACKAGE_VERSION << endl;
		    exit(0);
		}
		break;
	    case 'f':
		if (argv[0][2]) {
		    format = argv[0] + 2;
		    ++argv;
		    --argc;
		} else {
		    format = argv[1];
		    argv += 2;
		    argc -= 2;
		}
		continue;
	    case 'o':
		if (argv[0][2]) {
		    options = argv[0] + 2;
		    ++argv;
		    --argc;
		} else {
		    options = argv[1];
		    argv += 2;
		    argc -= 2;
		}
		continue;
	    case 'u':
		if (argv[0][2] != '\0') {
		    break;
		}
		url = true;
		++argv;
		--argc;
		continue;
	}

	cerr << "Option '" << argv[0] << "' unknown\n\n";
	argc = -1;
	break;
    }
last_option:

    if (argc != 2) {
	usage(cerr);
	_Exit(EX_USAGE);
    }

    const char * input = argv[0];
    const char * output = argv[1];

    void * handle = convert_init();
    if (!handle) {
	_Exit(EX_UNAVAILABLE);
    }
    int rc = convert(handle, url, input, output, format, options);
    convert_cleanup(handle);

    // Avoid segfault from LibreOffice by terminating swiftly.
    _Exit(rc);
}

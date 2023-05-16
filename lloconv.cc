/* lloconv.cc - Convert a document using LibreOfficeKit
 *
 * Copyright (C) 2014,2015,2016,2018 Olly Betts
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <config.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "convert.h"

using namespace std;

static void
usage(ostream& os)
{
    os << "Usage: " << program << " [-u|-s SOCKET_PATH] [-f OUTPUT_FORMAT] [-o OPTIONS] INPUT_FILE OUTPUT_FILE\n";
    os << "       " << program << " -s SOCKET_PATH -l\n\n";
    os << "  -u  INPUT_FILE is a URL\n\n";
    os << "Known values for OUTPUT_FORMAT include:\n";
    os << "  For text documents: doc docx fodt html odt ott pdf txt xhtml\n\n";
    os << "Known OPTIONS include:\n";
    os << "  EmbedImages - embed image data in HTML using <img src=\"data:...\">\n";
    os << "  SkipImages - don't include images\n";
    os << "  SkipHeaderFooter - don't include the header and footer\n";
    os << "  XHTML - use XHTML doctype (different to -f xhtml)\n";
    os << "  xhtmlns=foo - XHTML with HTML tags prefixed by foo:\n";
    os << flush;
}

static const int LISTEN_BACKLOG = 64;

// Automatically start a listener if -s is used there isn't one.
static bool auto_listener = true;

static bool
read_string(int fd, char ** s)
{
    unsigned char ch;
    if (read(fd, &ch, 1) != 1) return false;
    size_t len = ch;
    if (len >= 253) {
	unsigned i = len - 251;
	len = 0;
	while (i-- > 0) {
	    if (read(fd, &ch, 1) != 1) return false;
	    len = (len << 8) | ch;
	}
    }

    if (*s) free(*s);
    *s = (char *)malloc(len + 1);
    if (!*s) return false;

    char * p = *s;
    while (len) {
	ssize_t n = read(fd, p, len);
	if (n <= 0) {
	    // Error or EOF!
	    return false;
	}
	p += n;
	len -= n;
    }
    *p = '\0';

    return true;
}

static ssize_t
write_all(int fd, const char * buf, size_t count)
{
    while (count) {
	ssize_t r = write(fd, buf, count);
	if (r < 0) {
	    if (errno == EINTR) continue;
	    return r;
	}
	buf += r;
	count -= r;
    }
    return 0;
}

bool
write_string(int fd, const string & s)
{
    size_t len = s.size();
    char buf[5];
    size_t buf_len = 0;
    if (len < 253) {
	buf[buf_len++] = static_cast<unsigned char>(len);
    } else if (len < 0x10000) {
	buf[buf_len++] = 253;
	buf[buf_len++] = static_cast<unsigned char>(len >> 8);
	buf[buf_len++] = static_cast<unsigned char>(len);
	abort();
    } else if (len < 0x1000000) {
	buf[buf_len++] = 254;
	buf[buf_len++] = static_cast<unsigned char>(len >> 16);
	buf[buf_len++] = static_cast<unsigned char>(len >> 8);
	buf[buf_len++] = static_cast<unsigned char>(len);
	abort();
    } else {
	buf[buf_len++] = 255;
	buf[buf_len++] = static_cast<unsigned char>(len >> 24);
	buf[buf_len++] = static_cast<unsigned char>(len >> 16);
	buf[buf_len++] = static_cast<unsigned char>(len >> 8);
	buf[buf_len++] = static_cast<unsigned char>(len);
	abort();
    }
    if (buf_len != 1) abort();
    return write_all(fd, buf, buf_len) == 0 && write_all(fd, s.data(), len) == 0;
}

static void
read_params(int fd, char ** format, char ** input,
	    char ** output, char ** options)
{
    read_string(fd, format);
    read_string(fd, input);
    read_string(fd, output);
    read_string(fd, options);
}

static void
write_params(int fd, const char * format, const char * input,
	     const char * output, const char * options)
{
    if (!format) format = "";
    if (!options) options = "";
    write_string(fd, format);
    write_string(fd, input);
    write_string(fd, output);
    write_string(fd, options);
}

static int
read_result(int fd)
{
    char * v = NULL;
    if (!read_string(fd, &v)) return -1;
    int r = atoi(v);
    free(v);
    return r;
}

static void
write_result(int fd, int v)
{
    char buf[32];
    sprintf(buf, "%d", v);
    write_string(fd, buf);
}

static int
llo_daemon(const char * socket_path)
try {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
	perror("socket");
	return 1;
    }

    struct sockaddr_un my_addr;
    memset(&my_addr, 0, sizeof(struct sockaddr_un));
    my_addr.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(my_addr.sun_path)) {
	fprintf(stderr, "socket path too long\n");
	return 1;
    }
    strcpy(my_addr.sun_path, socket_path);

    if (bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
	perror("bind");
	return 1;
    }

    void * handle = convert_init();
    if (!handle) {
	return EX_UNAVAILABLE;
    }
    char * format = NULL;
    char * input = NULL;
    char * output = NULL;
    char * options = NULL;

    while (true) {
	if (listen(sock, LISTEN_BACKLOG) < 0) {
	    perror("listen");
	    return 1;
	}

	struct sockaddr_un peer_addr;
	socklen_t peer_addr_size = sizeof(struct sockaddr_un);
	int fd = accept(sock, (struct sockaddr *) &peer_addr, &peer_addr_size);
	if (fd < 0) {
	    perror("accept");
	    return 1;
	}

	read_params(fd, &format, &input, &output, &options);
	if (!format[0]) {
	    free(format);
	    format = NULL;
	}
	if (!options[0]) {
	    free(options);
	    options = NULL;
	}
	// Hard-code that the path is a file not a URL when using a server, at
	// least for now.
	int res = convert(handle, false, input, output, format, options);
	write_result(fd, res);
	close(fd);
    }
    convert_cleanup(handle);
} catch (const exception & e) {
    cerr << program << ": LibreOffice threw exception (" << e.what() << ")\n";
    return 1;
}

static int
llo_daemon_convert(int fd, const char * format, const char * input,
		   const char * output, const char * options)
{
    write_params(fd, format, input, output, options);
    return read_result(fd);
}

int
main(int argc, char **argv)
{
    program = argv[0];

    const char * format = NULL;
    const char * options = NULL;
    bool url = false;
    bool listener = false;
    const char * socket_path = NULL;

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
		    cout << "lloconv - " PACKAGE_STRING "\n";
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
	    case 'l':
		if (argv[0][2] != '\0') {
		    break;
		}
		listener = true;
		++argv;
		--argc;
		continue;
	    case 's':
		if (argv[0][2]) {
		    socket_path = argv[0] + 2;
		    ++argv;
		    --argc;
		} else {
		    socket_path = argv[1];
		    argv += 2;
		    argc -= 2;
		}
		continue;
	}

	cerr << "Option '" << argv[0] << "' unknown\n\n";
	argc = -1;
	break;
    }
last_option:

    if (listener) {
	if (argc != 0 || format || options || url || !socket_path) {
	    usage(cerr);
	    _Exit(EX_USAGE);
	}

	_Exit(llo_daemon(socket_path));
    }

    if ((url && socket_path) || argc != 2) {
	usage(cerr);
	_Exit(EX_USAGE);
    }

    const char * input = argv[0];
    const char * output = argv[1];

    if (socket_path) {
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
	    perror("socket");
	    _Exit(1);
	}

	struct sockaddr_un my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_un));
	my_addr.sun_family = AF_UNIX;
	if (strlen(socket_path) >= sizeof(my_addr.sun_path)) {
	    fprintf(stderr, "socket path too long\n");
	    _Exit(1);
	}
	strcpy(my_addr.sun_path, socket_path);

	if (connect(fd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
	    if ((errno != ECONNREFUSED && errno != ENOENT) || !auto_listener) {
		perror("connect");
		_Exit(1);
	    }
	    if (errno == ECONNREFUSED)
		unlink(socket_path);
	    pid_t child = fork();
	    if (child == -1) {
		perror("fork");
		_Exit(1);
	    }
	    if (child == 0) {
		_Exit(llo_daemon(socket_path));
	    }
	    // FIXME: Actually wait for daemon to start.
	    sleep(1);
	    if (connect(fd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
		perror("connect");
		_Exit(1);
	    }
	}

	int rc = llo_daemon_convert(fd, format, input, output, options);
	_Exit(rc);
    }

    void * handle = convert_init();
    if (!handle) {
	_Exit(EX_UNAVAILABLE);
    }
    int rc = convert(handle, url, input, output, format, options);
    convert_cleanup(handle);

    // Avoid segfault from LibreOffice by terminating swiftly.
    _Exit(rc);
}

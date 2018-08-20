EXTRA_PROGRAMS = inject-meta
bin_PROGRAMS = lloconv $(extra_programs)

noinst_HEADERS = convert.h urlencode.h

lloconv_SOURCES = lloconv.cc convert.cc urlencode.cc
lloconv_LDADD = -ldl

inject_meta_SOURCES = inject-meta.cc convert.cc urlencode.cc
inject_meta_LDADD = -ldl

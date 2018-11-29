dnl Process this file with autoconf to produce a configure script.

AC_INIT([lloconv], [6.1.0], [https://gitlab.com/ojwb/lloconv])
AM_INIT_AUTOMAKE([foreign -Wall -Wportability -Werror])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([lloconv.cc])

AC_PROG_CXX

dnl Enable extra warning flags for building with GCC.
if test yes = "$GCC"; then
  AM_CXXFLAGS="$AM_CXXFLAGS -Wall -W -Wshadow -Wpointer-arith -Wcast-align"
fi
AC_SUBST([AM_CXXFLAGS])

dnl Allow easy building against uninstalled LibreOffice.
AC_ARG_VAR([LO_INCLUDE_PATH], [Path to directory containing LibreOffice headers])
if test x"$LO_INCLUDE_PATH" != x ; then
  AM_CPPFLAGS="-I'$LO_INCLUDE_PATH'"
fi
AC_SUBST([AM_CPPFLAGS])

dnl Enable extra warning flags for building with GCC.
if test yes = "$GCC"; then
  AM_CXXFLAGS="$AM_CXXFLAGS -Wall -W -Wshadow -Wpointer-arith -Wcast-align"
fi
AC_SUBST([AM_CXXFLAGS])

AC_ARG_ENABLE([extra-programs],
[AS_HELP_STRING([--enable-extra-programs],
		[enable experimental extra programs])],
  [case ${enableval} in
    yes)
      extra_programs=inject-meta
      AC_SUBST([extra_programs]) ;;
    no) ;;
    *) AC_MSG_ERROR([bad value ${enableval} for --enable-extra-programs]) ;;
  esac])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_PREREQ([2.68])

AC_INIT([isp],
        [0.1],
        [],
        [],
        [])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign tar-pax subdir-objects])

AC_LANG([C])
AC_PROG_CC
AC_PROG_CC_STDC
AM_SILENT_RULES([yes])
AC_ENABLE_SHARED
AC_DISABLE_STATIC
AC_PROG_LIBTOOL(libtool)

AC_CONFIG_FILES([
        Makefile
	libisp/Makefile
	visp-test/Makefile
])

AC_OUTPUT

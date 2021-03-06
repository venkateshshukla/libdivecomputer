# Versioning.
m4_define([dc_version_major],[0])
m4_define([dc_version_minor],[5])
m4_define([dc_version_micro],[0])
m4_define([dc_version_suffix],[devel])
m4_define([dc_version],dc_version_major.dc_version_minor.dc_version_micro[]m4_ifset([dc_version_suffix],-[dc_version_suffix]))

# Libtool versioning.
m4_define([dc_version_lt_current],[0])
m4_define([dc_version_lt_revision],[0])
m4_define([dc_version_lt_age],[0])

# Initialize autoconf.
AC_PREREQ([2.60])
AC_INIT([libdivecomputer],[dc_version])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Initialize automake.
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

# Fix for automake >= 1.12
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

# Initialize libtool.
LT_PREREQ([2.2.0])
LT_INIT([win32-dll])
LT_PROG_RC

# Logging support.
AC_ARG_ENABLE([logging],
	[AS_HELP_STRING([--enable-logging=@<:@yes/no@:>@],
		[Enable logging @<:@default=yes@:>@])],
	[], [enable_logging=yes])
AS_IF([test "x$enable_logging" = "xyes"], [
	AC_DEFINE(ENABLE_LOGGING, [1], [Enable logging.])
])

# Pseudo terminal support.
AC_ARG_ENABLE([pty],
	[AS_HELP_STRING([--enable-pty=@<:@yes/no@:>@],
		[Enable pseudo terminal support @<:@default=no@:>@])],
	[], [enable_pty=no])
AS_IF([test "x$enable_pty" = "xyes"], [
	AC_DEFINE(ENABLE_PTY, [1], [Enable pseudo terminal support.])
])

# Example applications.
AC_ARG_ENABLE([examples],
	[AS_HELP_STRING([--enable-examples=@<:@yes/no@:>@],
		[Build example applications @<:@default=yes@:>@])],
	[], [enable_examples=yes])
AM_CONDITIONAL([ENABLE_EXAMPLES], [test "x$enable_examples" = "xyes"])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99

# Enable automake silent build rules.
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# Checks for native Windows.
AC_MSG_CHECKING([for native Win32])
case "$host" in
  *-*-mingw*)
    os_win32=yes
    ;;
  *)
    os_win32=no
    ;;
esac
AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL([OS_WIN32], [test "$os_win32" = "yes"])

# Checks for Android.
AC_MSG_CHECKING([for Android])
case "$host" in
  *-linux-android*)
    os_android=yes
    ;;
  *)
    os_android=no
    ;;
esac
AC_MSG_RESULT([$os_android])
AM_CONDITIONAL([OS_ANDROID], [test "$os_android" = "yes"])

# Checks for USB support.
PKG_CHECK_MODULES([LIBUSB], [libusb-1.0], [have_libusb=yes], [have_libusb=no])
if test "$have_libusb" = "yes"; then
	AC_DEFINE([HAVE_LIBUSB], [1], [libusb support])
	dependencies="libusb-1.0"
fi

# Checks for FTDI support.
PKG_CHECK_MODULES([LIBFTDI1], [libftdi1], [have_libftdi1=yes], [have_libftdi1=no])
if test "$have_libftdi1" = "yes"; then
	AC_DEFINE([HAVE_LIBFTDI1], [1], [libftdi support])
	dependencies+=" libftdi1"
fi

if test "$os_android" = yes; then
       # Checks for FTDI support.
       PKG_CHECK_MODULES([LIBFTDI1], [libftdi1], [have_libftdi1=yes], [have_libftdi1=no])
       if test "$have_libftdi1" = "yes"; then
               AC_DEFINE([HAVE_LIBFTDI1], [1], [libftdi support])
               dependencies+=" libftdi1"
       fi
       # Checking for presence of ftdi_usb_open_fd function in libftdi
       AC_SEARCH_LIBS([ftdi_usb_open_fd], [ftdi1], [have_libftdi_fd=yes], [have_libftdi_fd=no], [-lusb-1.0])
       if test "$have_libftdi_fd" = "yes"; then
	       AC_DEFINE([HAVE_LIBFTDI_FD], [1], [ftdi_usb_open_fd function])
       fi
fi

AC_SUBST([DEPENDENCIES], [$dependencies])

# Checks for IrDA support.
AC_CHECK_HEADERS([winsock2.h af_irda.h], [irda_win32=yes], [irda_win32=no], [
#if HAVE_WINSOCK2_H
#   include <winsock2.h>
# endif
])

AC_CHECK_HEADERS([sys/socket.h linux/types.h linux/irda.h], [irda_linux=yes], [irda_linux=no], [
#if HAVE_SYS_SOCKET_H
#   include <sys/socket.h>
# endif
#if HAVE_LINUX_TYPES_H
#   include <linux/types.h>
# endif
])

if test "$irda_win32" = "yes" || test "$irda_linux" = "yes"; then
	AC_DEFINE([HAVE_IRDA], [1], [IrDA support])
fi

AM_CONDITIONAL([IRDA], [test "$irda_win32" = "yes" || test "$irda_linux" = "yes"])

# Checks for header files.
AC_CHECK_HEADERS([linux/serial.h])
AC_CHECK_HEADERS([IOKit/serial/ioss.h])

# Checks for library functions.
AC_FUNC_STRERROR_R
AC_CHECK_FUNCS([localtime_r gmtime_r])

# Versioning.
AC_SUBST([DC_VERSION],[dc_version])
AC_SUBST([DC_VERSION_MAJOR],[dc_version_major])
AC_SUBST([DC_VERSION_MINOR],[dc_version_minor])
AC_SUBST([DC_VERSION_MICRO],[dc_version_micro])
AC_SUBST([DC_VERSION_LIBTOOL],[dc_version_lt_current:dc_version_lt_revision:dc_version_lt_age])

# Version suffix.
m4_ifset([dc_version_suffix],[
	AC_DEFINE(HAVE_VERSION_SUFFIX, [1], [Define if a version suffix is present.])
])

AC_CONFIG_FILES([
   libdivecomputer.pc
   Makefile
   include/Makefile
   include/libdivecomputer/Makefile
   include/libdivecomputer/version.h
   src/Makefile
   src/libdivecomputer.rc
   examples/Makefile
])
AC_OUTPUT

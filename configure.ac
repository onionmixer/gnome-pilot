dnl ******************************
dnl Version
dnl ******************************
m4_define([gnome_pilot_major], [4])
m4_define([gnome_pilot_revision], [0])
m4_define([gnome_pilot_patchlevel], [1])
m4_define([gnome_pilot_version], 
	[gnome_pilot_major.gnome_pilot_revision.gnome_pilot_patchlevel])


dnl *****************************
dnl Initialize autoconf/automake
dnl *****************************
AC_PREREQ(2.58)
AC_INIT([gnome-pilot],[gnome_pilot_version],[https://bugzilla.gnome.org//enter_bug.cgi?product=gnome-pilot])
AM_INIT_AUTOMAKE([1.9 -Wall foreign])
AC_CONFIG_MACRO_DIR([macros])
AC_CONFIG_HEADERS(config.h)
AC_CONFIG_SRCDIR([Makefile.am])

dnl ******************************
dnl Library Versions
dnl ******************************
GPILOTD_CURRENT=0
GPILOTD_REVISION=1
GPILOTD_AGE=0

AC_SUBST(GPILOTD_CURRENT)
AC_SUBST(GPILOTD_REVISION)
AC_SUBST(GPILOTD_AGE)

GPILOTD_CONDUIT_CURRENT=0
GPILOTD_CONDUIT_REVISION=1
GPILOTD_CONDUIT_AGE=0

AC_SUBST(GPILOTD_CONDUIT_CURRENT)
AC_SUBST(GPILOTD_CONDUIT_REVISION)
AC_SUBST(GPILOTD_CONDUIT_AGE)

GPILOTD_CM_CURRENT=0
GPILOTD_CM_REVISION=1
GPILOTD_CM_AGE=0

AC_SUBST(GPILOTD_CM_CURRENT)
AC_SUBST(GPILOTD_CM_REVISION)
AC_SUBST(GPILOTD_CM_AGE)

BASE_VERSION=4.0
AC_SUBST(BASE_VERSION)
AC_DEFINE_UNQUOTED(BASE_VERSION, "$BASE_VERSION", "Base version (Major.Minor)")

dnl ******************************
dnl Basic Checks
dnl ******************************
AM_MAINTAINER_MODE

AM_PROG_LIBTOOL
dnl Intltool removed — using gettext native features (msgfmt --desktop)
dnl AM_GLIB_GNU_GETTEXT (below) provides gettext infrastructure
PKG_PROG_PKG_CONFIG

AC_PROG_CC
AC_PROG_CXX
AC_PROG_YACC
AC_HEADER_STDC
AC_SEARCH_LIBS([strerror],[cposix])

GNOME_COMPILE_WARNINGS
CFLAGS="$CFLAGS $WARN_CFLAGS"

dnl ******************************
dnl Pilot Link Check
dnl ******************************
PILOT_LINK_CHECK(0.12.0)
AC_SUBST(PILOT_LINK_MAJOR)
AC_SUBST(PILOT_LINK_MINOR)
AC_SUBST(PILOT_LINK_MICRO)
AC_SUBST(PILOT_LINK_PATCH)
AC_SUBST(PILOT_LINK_VERSION)

dnl ******************************
dnl Various Device Support Checks
dnl ******************************
AC_ARG_ENABLE([usb],
	[AS_HELP_STRING([--enable-usb],
	[Enable support for the Handspring USB cradle.])],
	[do_usb="$enableval"], [do_usb="yes"])

if test  x"$do_usb" = x"yes"; then
	AC_DEFINE(WITH_USB_VISOR,, "With USB Visor Support")
fi

AC_DEFINE(WITH_IRDA,,"With IrDA Support")

AC_ARG_ENABLE([network],
	[AS_HELP_STRING([--enable-network],
	[Enable support for network synchronization])],
	[do_network="$enableval"],[do_network="yes"])

if test x"$do_network" = x"yes"; then
	AC_DEFINE(WITH_NETWORK,,"With Network Sync Support")
fi

dnl *********************************
dnl Evolution-Data-Server Integration
dnl *********************************
AC_ARG_ENABLE([eds-conduits],
	[AS_HELP_STRING([--enable-eds-conduits],
	[Enable Evolution-Data-Server conduits])],
	[enable_eds="$enableval"],[enable_eds="yes"])
if test "x$enable_eds" = "xyes"; then
	PKG_CHECK_MODULES(EVOLUTION_DATA_SERVER,
	[libebook-1.2 libecal-2.0 libedataserver-1.2])
	EDS_PC_REQUIRES="libedataserver-1.2 libebook-1.2 libecal-2.0"
else
	EDS_PC_REQUIRES=""
fi
AC_SUBST(EDS_PC_REQUIRES)
AM_CONDITIONAL(ENABLE_EDS_CONDUITS, test "x$enable_eds" = "xyes")

dnl ******************************
dnl Library Checks
dnl ******************************
LIBXML_REQUIRED="libxml-2.0"
LIBGTK_REQUIRED="gtk4 >= 4.6"
GUDEV_REQUIRED="gudev-1.0"
GIO_REQUIRED="gio-2.0 >= 2.72"

PKG_CHECK_MODULES(GNOME_PILOT,
[
	$LIBXML_REQUIRED
	$LIBGTK_REQUIRED
	$GIO_REQUIRED
	$GUDEV_REQUIRED
])

GNOME_PILOT_CFLAGS="$GNOME_PILOT_CFLAGS $PISOCK_CFLAGS"
GNOME_PILOT_LIBS="$GNOME_PILOT_LIBS $PISOCK_LIBS"

AC_SUBST(GNOME_PILOT_CFLAGS)
AC_SUBST(GNOME_PILOT_LIBS)

dnl *********************************
dnl System Tray (StatusNotifierItem via D-Bus)
dnl *********************************
AC_ARG_ENABLE([tray],
	[AS_HELP_STRING([--enable-tray],
	[Enable system tray icon using StatusNotifierItem D-Bus protocol])],
	[enable_tray="$enableval"],[enable_tray="auto"])

have_dbusmenu=no
if test "x$enable_tray" != "xno"; then
	PKG_CHECK_MODULES(DBUSMENU,
		[dbusmenu-glib-0.4],
		[have_dbusmenu=yes],
		[have_dbusmenu=no])
	if test "x$have_dbusmenu" = "xyes"; then
		AC_DEFINE(HAVE_DBUSMENU, 1, [Have libdbusmenu-glib])
	elif test "x$enable_tray" = "xyes"; then
		AC_MSG_ERROR([Tray requested but dbusmenu-glib-0.4 not found])
	fi
fi
AM_CONDITIONAL(ENABLE_TRAY, test "x$have_dbusmenu" = "xyes")
AC_SUBST(DBUSMENU_CFLAGS)
AC_SUBST(DBUSMENU_LIBS)

dnl GOB2 removed — source files are now directly maintained

dnl ******************************
dnl gdbus-codegen (for GDBus generated code)
dnl ******************************
AC_PATH_PROG(GDBUS_CODEGEN, gdbus-codegen)

dnl GSettings
GLIB_GSETTINGS

dnl **************************************************
dnl * Marshaller generation
dnl **************************************************
GLIB_GENMARSHAL="`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`"
AC_SUBST(GLIB_GENMARSHAL)

dnl **************************************************
dnl * Conduit files
dnl **************************************************
GP_CONDUIT_RULE=$srcdir/conduit.mk
AC_SUBST_FILE(GP_CONDUIT_RULE)

dnl **************************************************
dnl * Conduit files
dnl **************************************************
GP_SERVER_RULE=$srcdir/server.mk
AC_SUBST_FILE(GP_SERVER_RULE)

dnl **************************************************
dnl * internationalization support
dnl **************************************************
GETTEXT_PACKAGE=gnome-pilot
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", "Package name")
AM_GLIB_GNU_GETTEXT


AC_DEFINE_UNQUOTED(GNOME_ICONDIR, "${prefix}/share/pixmaps", "Icon directory")

dnl Check for lib socket for solaris users.  Do you guys need -lnsl for pisock as well?  Been too long since I used solaris :)
AC_CHECK_LIB(socket, sendmsg, [LIBS="-lsocket -lnsl"], [])

dnl Set platform specific libs and defines here 
dnl For defines, remember an addition to acconfig.h
case "$host" in
	*solaris*)
	LIBS="$LIBS -lnsl"
	;;
	*linux*)
	AC_DEFINE(USE_XOPEN_SOURCE,,"Use XOpen Source")
	;;
esac

AC_CHECK_FUNCS(crypt)
if test $ac_cv_func_crypt = no; then
  # SCO-ODT-3.0 is reported to need this library for crypt.
  AC_CHECK_LIB(ufc, crypt, LIBS="$LIBS -lufc",
    [# NetBSD needs this library for crypt.
    AC_CHECK_LIB(crypt, crypt,[ LIBS="$LIBS -lcrypt" ],[ AC_MSG_ERROR("Unable to find crypt")])])
fi

dnl GNOME_DOC_INIT — disabled (gnome-doc-utils is deprecated, use yelp-tools)

dnl ******************************
dnl Special directories
dnl ******************************

dnl If you add something here, consider whether or not you also
dnl need to add it to the .pc.in file

privincludedir='${includedir}'/gnome-pilot-$BASE_VERSION
AC_SUBST(privincludedir)


dnl Create files.
AC_CONFIG_FILES([
Makefile 
gpilotd/Makefile
applet/Makefile
conduits/Makefile
conduits/backup/Makefile
conduits/evolution-data-server/Makefile
conduits/file/Makefile
conduits/test/Makefile
capplet/Makefile
utils/Makefile
po/Makefile.in
help/Makefile
gnome-pilot-4.0.pc
])
AC_OUTPUT

AC_MSG_NOTICE([
Configuration :
	network sync : $do_network
	usb          : $do_usb
	EDS conduits : $enable_eds
	tray icon    : $have_dbusmenu
	pilot-link   : $PILOT_LINK_VERSION
])
#AC_CONFIG_MACRO_DIR([m4])

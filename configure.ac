dnl required version of autoconf
AC_PREREQ([2.53])

dnl TODO: fill in your package name and package version here
AC_INIT([gst-sh-mobile],[0.10.1])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=0.10.16
GSTPB_REQUIRED=0.10.16

AC_CONFIG_SRCDIR([gst/shvideo/gstshvideoplugin.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([1.10])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC
AC_PROG_LIBTOOL

dnl TODO required version of libtool
dnl LT_PREREQ([2.2.6])
LT_INIT

dnl Check for doxygen
AC_CHECK_PROG(HAVE_DOXYGEN, doxygen, true, false)
AM_CONDITIONAL(HAVE_DOXYGEN,$HAVE_DOXYGEN)
if test $HAVE_DOXYGEN = "false"; then
	AC_MSG_WARN([*** doxygen not found, docs will not be built])
fi

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
PKG_CHECK_MODULES(GST, [
  gstreamer-0.10 >= $GST_REQUIRED
  gstreamer-base-0.10 >= $GST_REQUIRED
  gstreamer-controller-0.10 >= $GST_REQUIRED
  gstreamer-video-0.10 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer0.10-dev and libgstreamer-plugins-base0.10-dev.
      on RPM-based systems gstreamer0.10-devel, libgstreamer0.10-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-0.10/plugins"
else
  plugindir="\$(libdir)/gstreamer-0.10"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)


dnl
dnl Check for libraries
dnl
PKG_CHECK_MODULES(UIOMUX, uiomux >= 1.5.0)
PKG_CHECK_MODULES(SHVEU, shveu >= 1.5.0)
PKG_CHECK_MODULES(SHCODECS, shcodecs >= 1.4.0)

dnl
dnl Check for Scale plugin enabled
dnl
AC_ARG_ENABLE(scale, AS_HELP_STRING([--disable-scale], [Disable scale (VEU) plugin]))

AS_IF([test "x$enable_scale" != "xno"], [
	dnl Scale plugin enabled - what about libshveu?
    PKG_CHECK_MODULES(SHVEU, shveu >= 1.5.0, [SCALE=yes],
		[AC_MSG_WARN([libshveu not found - scale plugin won't be built])])
])
if test "x$SCALE" = "xyes" ; then
	AC_DEFINE(ENABLE_SCALE, [], [Scale])
fi
AM_CONDITIONAL(ENABLE_SCALE, [test "x$SCALE" = "xyes"])


dnl
dnl Check for Blend plugin enabled
dnl
AC_ARG_ENABLE(blend, AS_HELP_STRING([--disable-blend], [Disable blend (BEU) plugin]))

AS_IF([test "x$enable_blend" != "xno"], [
	dnl Blend plugin enabled - what about libshbeu?
    PKG_CHECK_MODULES(SHBEU, shbeu >= 1.0.0, [BLEND=yes],
		[AC_MSG_WARN([libshbeu not found - blend plugin won't be built])])
])
if test "x$BLEND" = "xyes" ; then
	AC_DEFINE(ENABLE_BLEND, [], [Blend])
fi
AM_CONDITIONAL(ENABLE_BLEND, [test "x$BLEND" = "xyes"])


AC_CONFIG_FILES([
Makefile
gst/Makefile
gst/shvideo/Makefile
doc/Makefile
doc/Doxyfile
])
AC_OUTPUT



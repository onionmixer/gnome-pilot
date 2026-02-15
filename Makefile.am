ACLOCAL_AMFLAGS=-I macros

SUBDIRS = 			\
	po 			\
	gpilotd 		\
	capplet 		\
	conduits 		\
	utils			\
	help			\
	applet

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=gnome-pilot-4.0.pc

# ignore the scrollkeeper files on uninstall check
distuninstallcheck_listfiles = find . -type f -print | grep -v \
 '^\./var/scrollkeeper'

changelogs =				\
	applet/OChangeLog		\
	capplet/OChangeLog		\
	conduits/backup/OChangeLog	\
	conduits/OChangeLog		\
	conduits/file/OChangeLog	\
	gpilotd/OChangeLog		\
	utils/OChangeLog

EXTRA_DIST=			\
	$(changelogs)		\
	conduit.mk		\
	gnome-doc-utils.make	\
	server.mk		\
	gnome-pilot-4.0.pc.in

DISTCLEANFILES = 		\
	gnome-doc-utils.make

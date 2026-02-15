NULL=

uidir  = $(datadir)/gnome-pilot/ui
ui_DATA = pilot-applet.ui

pixmapdir = $(datadir)/pixmaps
pixmap_DATA = sync_paused.png sync_broken.png sync_icon.png syncing_icon.png

gsettings_SCHEMAS = org.gnome.GnomePilot.applet.gschema.xml
@GSETTINGS_RULES@

AM_CPPFLAGS = 						\
	-I$(top_srcdir)					\
	-I$(top_builddir)/gpilotd -I$(top_srcdir)/gpilotd \
	$(GNOME_PILOT_CFLAGS)				\
	-DUIDATADIR=\""$(uidir)"\" 		\
	-DPREFIX=\""$(prefix)\"" 			\
	-DSYSCONFDIR=\""$(sysconfdir)\"" 		\
	-DDATADIR=\""$(datadir)\"" 			\
	-DLIBDIR=\""$(libdir)\"" 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"

if ENABLE_TRAY
bin_PROGRAMS = gpilot-tray

gpilot_tray_SOURCES = \
	gpilot-tray.c \
	gpilot-tray-sni.h \
	gpilot-applet-progress.c \
	gpilot-applet-progress.h \
	gpilot-applet-progress-private.h

gpilot_tray_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(DBUSMENU_CFLAGS)

gpilot_tray_LDADD = \
	$(GNOME_PILOT_LIBS) \
	$(DBUSMENU_LIBS) \
	$(top_builddir)/gpilotd/libgpilotd-4.0.la
endif

EXTRA_DIST = 				\
	$(pixmap_DATA) 			\
	$(ui_DATA) 			\
	$(gsettings_SCHEMAS)		\
	$(NULL)

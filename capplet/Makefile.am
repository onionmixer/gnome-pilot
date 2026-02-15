NULL=

bin_PROGRAMS = gpilotd-control-applet 

AM_CPPFLAGS = 						\
	-I$(top_srcdir) 				\
	-I$(top_builddir)/gpilotd			\
	-I$(top_srcdir)/gpilotd				\
	$(GNOME_PILOT_CFLAGS) 				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNOMEPIXMAPSDIR=\""$(datadir)/pixmaps"\" 	\
	-DUIDATADIR=\""$(uidir)"\" 			\
	$(NULL)

gpilotd_control_applet_SOURCES = 	\
	gpilotd-control-applet.c 	\
	gnome-pilot-capplet.c		\
	gnome-pilot-capplet.h		\
	gnome-pilot-pdialog.c		\
	gnome-pilot-pdialog.h		\
	gnome-pilot-ddialog.c		\
	gnome-pilot-ddialog.h		\
	gnome-pilot-cdialog.c		\
	gnome-pilot-cdialog.h		\
	gnome-pilot-assistant.c		\
	gnome-pilot-assistant.h		\
	util.c				\
	util.h				\
	pilot.c 			\
	pilot.h				\
	$(NULL)

gpilotd_control_applet_LDFLAGS = \
	-export-dynamic

gpilotd_control_applet_LDADD = \
	$(top_builddir)/gpilotd/libgpilotd-4.0.la 			\
	$(top_builddir)/gpilotd/libgpilotdconduit-4.0.la    	\
	$(top_builddir)/gpilotd/libgpilotdcm-4.0.la    	\
	$(GNOME_PILOT_LIBS) 					\
	$(NULL)

desktopdir = $(datadir)/applications
#desktop_in_files = gpilotd-control-applet.desktop.in
#desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

desktop_in_in_file = gpilotd-control-applet.desktop.in.in
desktop_DATA = $(desktop_in_in_file:.desktop.in.in=.desktop)
%.desktop.in: %.desktop.in.in
	sed -e "s|\@VERSION\@|$(VERSION)|" $< > $@

%.desktop: %.desktop.in $(wildcard $(top_srcdir)/po/*.po)
	$(AM_V_GEN) $(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@

uidir  = $(datadir)/gnome-pilot/ui
ui_DATA = 				\
	gpilotd-capplet.ui 		\
        gnome-pilot-watermark.png 	\
	gnome-palm.png			\
	$(NULL)

appicondir = $(datadir)/pixmaps
appicon_DATA = 			\
	gnome-palm.png		\
	gnome-palm-conduit.png	\
	$(NULL)


EXTRA_DIST = 				\
	$(desktop_in_in_file) 		\
	$(appicon_DATA)			\
	$(ui_DATA) 			\
	$(NULL)

BUILT_SOURCES = $(desktop_DATA)

CLEANFILES = $(BUILT_SOURCES)

AM_CPPFLAGS = 					\
	-I$(top_srcdir)	 			\
	-I$(top_builddir)/gpilotd		\
	-I$(top_srcdir)/gpilotd			\
	$(GNOME_PILOT_CFLAGS) 			\
	-DG_LOG_DOMAIN=\"fileconduit\" 		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"


file_conduitsdir=$(libdir)/gnome-pilot/conduits

file_conduits_LTLIBRARIES = \
	libfile_conduit-4.0.la

libfile_conduit_4_0_la_SOURCES = file_conduit.c file_conduit.h palm.keys.in

libfile_conduit_4_0_la_LDFLAGS = -module -avoid-version
libfile_conduit_4_0_la_LIBADD = 		\
	$(GNOME_PILOT_LIBS)  \
	$(top_builddir)/gpilotd/libgpilotdconduit-4.0.la


bin_PROGRAMS = gpilot-install-file

gpilot_install_file_SOURCES = gpilot_install_file.c

gpilot_install_file_LDADD = 					\
		$(top_builddir)/gpilotd/libgpilotd-4.0.la 		\
		$(GNOME_PILOT_LIBS)

@GP_CONDUIT_RULE@

Conduitdir = $(datadir)/gnome-pilot/conduits/
Conduit_DATA = file.conduit

appicondir = $(datadir)/pixmaps

palm.keys: palm.keys.in Makefile
	sed -e 's^\@icondir\@^$(appicondir)^g' < $(srcdir)/palm.keys.in > palm.keys.tmp \
	&& mv palm.keys.tmp palm.keys 


Mimedir = $(datadir)/mime-info
Mime_DATA = palm.keys


install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(Conduitdir)

man_MANS=gpilot-install-file.1

CLEANFILES = $(Conduit_DATA) $(Mime_DATA)

EXTRA_DIST = 			\
	file.conduit.in		\
	$(man_MANS)		\
	$(Mime_DATA)

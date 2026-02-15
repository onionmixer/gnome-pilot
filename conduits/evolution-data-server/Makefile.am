moduledir = $(libdir)/gnome-pilot/conduits/
conduitdir = $(datadir)/gnome-pilot/conduits/

module_LTLIBRARIES = \
	libeaddress_conduit-4.0.la \
	libecalendar_conduit-4.0.la \
	libememo_conduit-4.0.la \
	libetodo_conduit-4.0.la

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir)/gpilotd -I$(top_srcdir)/gpilotd \
	$(GNOME_PILOT_CFLAGS) \
	$(EVOLUTION_DATA_SERVER_CFLAGS) \
	-DG_LOG_DOMAIN=\"eds-conduits\"

libeaddress_conduit_4_0_la_SOURCES = \
	address-conduit.c \
	e-dialog-widgets.c \
	e-dialog-widgets.h \
	e-pilot-map.c \
	e-pilot-map.h \
	e-pilot-settings.c \
	e-pilot-settings.h \
	e-pilot-util.c \
	e-pilot-util.h

libeaddress_conduit_4_0_la_LDFLAGS = \
	-module -avoid-version

libeaddress_conduit_4_0_la_LIBADD = \
	$(GNOME_PILOT_LIBS) \
	$(EVOLUTION_DATA_SERVER_LIBS)

libecalendar_conduit_4_0_la_SOURCES = \
	calendar-conduit.c \
	libecalendar-common-conduit.c \
	libecalendar-common-conduit.h \
	e-pilot-map.c \
	e-pilot-map.h \
	e-pilot-settings.c \
	e-pilot-settings.h \
	e-pilot-util.c \
	e-pilot-util.h

libecalendar_conduit_4_0_la_LDFLAGS = \
	-module -avoid-version

libecalendar_conduit_4_0_la_LIBADD = \
	$(LIBECAL_LIBS) \
	$(EVOLUTION_DATA_SERVER_LIBS)

libememo_conduit_4_0_la_SOURCES = \
	memo-conduit.c \
	libecalendar-common-conduit.c \
	libecalendar-common-conduit.h \
	e-pilot-map.c \
	e-pilot-map.h \
	e-pilot-settings.c \
	e-pilot-settings.h \
	e-pilot-util.c \
	e-pilot-util.h

libememo_conduit_4_0_la_LDFLAGS = \
	-module -avoid-version

libememo_conduit_4_0_la_LIBADD = \
	$(LIBECAL_LIBS) \
	$(EVOLUTION_DATA_SERVER_LIBS)

libetodo_conduit_4_0_la_SOURCES = \
	todo-conduit.c \
	libecalendar-common-conduit.c \
	libecalendar-common-conduit.h \
	e-pilot-map.c \
	e-pilot-map.h \
	e-pilot-settings.c \
	e-pilot-settings.h \
	e-pilot-util.c \
	e-pilot-util.h

libetodo_conduit_4_0_la_LDFLAGS = \
	-module -avoid-version

libetodo_conduit_4_0_la_LIBADD = \
	$(GNOME_PILOT_LIBS) \
	$(EVOLUTION_DATA_SERVER_LIBS)

@GP_CONDUIT_RULE@

conduit_DATA = \
	e-address.conduit \
	e-calendar.conduit \
	e-memo.conduit \
	e-todo.conduit

BUILT_SOURCES = $(conduit_DATA)
CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = \
	e-address.conduit.in \
	e-calendar.conduit.in \
	e-memo.conduit.in \
	e-todo.conduit.in

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

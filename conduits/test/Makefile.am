AM_CPPFLAGS = 				\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/gpilotd		\
	-I$(top_builddir)/gpilotd	\
	$(GNOME_PILOT_CFLAGS)

test_conduitsdir=$(libdir)/gnome-pilot/conduits

test_conduits_LTLIBRARIES = \
	libtest_conduit-4.0.la

libtest_conduit_4_0_la_SOURCES = 	\
	test_conduit.c

libtest_conduit_4_0_la_LDFLAGS = -module -avoid-version
libtest_conduit_4_0_la_LIBADD = 	\
	$(GNOME_PILOT_LIBS) \
	$(top_builddir)/gpilotd/libgpilotdconduit-4.0.la

@GP_CONDUIT_RULE@

Conduitdir = $(datadir)/gnome-pilot/conduits/
Conduit_DATA = test.conduit

CLEANFILES = $(Conduit_DATA)

EXTRA_DIST = 		\
	test.conduit.in

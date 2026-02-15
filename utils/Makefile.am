AM_CPPFLAGS = 								\
	-I$(top_srcdir)							\
	-I$(top_builddir)/gpilotd					\
	-I$(top_srcdir)/gpilotd						\
	$(GNOME_PILOT_CFLAGS) 						\
        -DG_LOG_DOMAIN=\"gpilotd-client\" 				\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"

bin_PROGRAMS = gnome-pilot-make-password
noinst_PROGRAMS = gpilotd-client gpilotdcm-client

gnome_pilot_make_password_SOURCES = make-password.c
gnome_pilot_make_password_LDADD = 	\
	$(GNOME_PILOT_LIBS)

gpilotd_client_SOURCES = gpilotd-client.c
gpilotd_client_LDADD = 				\
	$(top_builddir)/gpilotd/libgpilotd-4.0.la 	\
	$(GNOME_PILOT_LIBS)

gpilotdcm_client_SOURCES = gpilotdcm-client.c
gpilotdcm_client_LDADD = 					\
	$(top_builddir)/gpilotd/libgpilotdconduit-4.0.la	 	\
	$(top_builddir)/gpilotd/libgpilotd-4.0.la			\
	$(top_builddir)/gpilotd/libgpilotdcm-4.0.la 		\
	$(GNOME_PILOT_LIBS)

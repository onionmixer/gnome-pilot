/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
 * Copyright (C) 1998-2000 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Eskil Heyn Olsen
 *          Vadim Strizhevsky
 *          Manish Vachharajani
 *          Dave Camp
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <gio/gio.h>

#include "gpilot-daemon.h"

#define GP_DBUS_NAME         "org.gnome.GnomePilot"

static void remove_pid_file (void);

static void 
sig_hup_handler (int dummy)
{
	signal (SIGHUP, sig_hup_handler);
}

static void 
sig_term_handler (int dummy)
{
	g_message (_("Exiting (caught SIGTERM)..."));
	remove_pid_file ();
	exit (0);
}

static void 
sig_int_handler (int dummy)
{
	g_message (_("Exiting (caught SIGINT)..."));
	remove_pid_file ();
	exit (0);
}

/* This deletes the ~/.gpilotd.pid file */
static void 
remove_pid_file (void)
{
	char *filename;
	
	filename = g_build_filename (g_get_home_dir (), ".gpilotd.pid", NULL);
	unlink (filename);
	g_free (filename);
}

/*
  The creates a ~/.gilotd.pid, containing the pid
   of the gpilotd process, used by clients to send
   SIGHUPS
*/
static void 
write_pid_file (void)
{
	char *filename, *dirname, *buf;
	size_t nwritten = 0;
	ssize_t n, w;
	int fd;
	
	dirname = g_build_filename (g_get_home_dir (), ".gpilotd", NULL);
	if (!g_file_test (dirname, G_FILE_TEST_IS_DIR) && mkdir (dirname, 0777) != 0)
		g_warning (_("Unable to create file installation queue directory"));
	g_free (dirname);
	
	filename = g_build_filename (g_get_home_dir (), ".gpilotd.pid", NULL);
	fd = open (filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
	g_free (filename);
	
	if (fd == -1)
		return;
	
	buf = g_strdup_printf ("%lu", (unsigned long) getpid ());
	n = strlen (buf);
	
	do {
		do {
			w = write (fd, buf + nwritten, n - nwritten);
		} while (w == -1 && errno == EINTR);
		
		if (w == -1)
			break;
		
		nwritten += w;
	} while (nwritten < n);
	
	/* FIXME: what do we do if the write is incomplete? (e.g. nwritten < n)*/
	
	fsync (fd);
	close (fd);
}

/* This function display which pilot-link version was used
   and which features were enabled at compiletime */
static void
dump_build_info (void)
{
	GString *str = g_string_new (NULL);
	g_message ("compiled for pilot-link version %s",
		   GP_PILOT_LINK_VERSION);

	str = g_string_append (str, "compiled with ");
#ifdef WITH_VFS
	str = g_string_append (str, "[VFS] ");
#endif
#ifdef WITH_USB_VISOR
	str = g_string_append (str, "[USB] ");
#endif
#ifdef WITH_IRDA
	str = g_string_append (str, "[IrDA] ");
#endif
#ifdef WITH_NETWORK
	str = g_string_append (str, "[Network] ");
	str = g_string_append (str, "[Bluetooth] ");
#endif
	g_message ("%s", str->str);
	g_string_free (str, TRUE);
}

static GMainLoop *main_loop = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
        /* TODO: register D-Bus objects here in Phase 2 */
        g_debug ("Bus acquired for %s", name);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
        g_message ("Acquired D-Bus name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
        if (connection == NULL) {
                g_warning ("Couldn't connect to session bus");
        } else {
                g_warning (_("gpilotd already running; exiting ...\n"));
        }
        if (main_loop != NULL)
                g_main_loop_quit (main_loop);
}

int
main (int argc, char *argv[])
{
        GpilotDaemon    *daemon;
        guint            owner_id;
        int              ret;

	ret = 1;
	/* Intro */
	g_message ("%s %s starting...", PACKAGE, VERSION);
	dump_build_info ();

	/* Setup the correct gpilotd.pid file */
	remove_pid_file ();
	write_pid_file ();

	/* GTK4: gtk_init() takes no arguments */
	gtk_init ();

        daemon = gpilot_daemon_new ();
        if (daemon == NULL) {
                goto out;
        }

        /* GDBus: own the bus name asynchronously */
        owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                   GP_DBUS_NAME,
                                   G_BUS_NAME_OWNER_FLAGS_NONE,
                                   on_bus_acquired,
                                   on_name_acquired,
                                   on_name_lost,
                                   daemon,
                                   NULL);

        main_loop = g_main_loop_new (NULL, FALSE);

	signal (SIGTERM, sig_term_handler);
	signal (SIGINT, sig_int_handler);
	signal (SIGHUP, sig_hup_handler);

        g_main_loop_run (main_loop);

        g_bus_unown_name (owner_id);

        if (daemon != NULL) {
                g_object_unref (daemon);
        }

	/* It is unlikely that we will end here */
	remove_pid_file ();

	ret = 0;
 out:
        return ret;
}

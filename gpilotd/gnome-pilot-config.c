/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//*
 * gnome-pilot-config.c
 *
 * Copyright (C) 2009 Free Software Foundation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Halton Huo <halton.huo@sun.com>
 *
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "gnome-pilot-config.h"

#define OLD_PREFIX ".gnome2/gnome-pilot.d"
#define NEW_PREFIX ".gnome-pilot"

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')

static void
migrate_conf (const gchar *old, const gchar *new)
{
	gchar *basename = g_path_get_dirname (new);

	if (!g_file_test (basename, G_FILE_TEST_EXISTS)) {
		g_mkdir_with_parents (basename, S_IRUSR | S_IWUSR | S_IXUSR);
	} else {
		if (!g_file_test (basename, G_FILE_TEST_IS_DIR)) {
			gchar *tmp = g_strdup_printf ("%s.old", basename);
			rename (basename, tmp);
			g_free (tmp);
			g_mkdir_with_parents (basename, S_IRUSR | S_IWUSR | S_IXUSR);
		}
	}
	g_free (basename);

	if (g_file_test (new, G_FILE_TEST_IS_REGULAR)) {
		return;
	} else if (g_file_test (old, G_FILE_TEST_IS_REGULAR)) {
		rename (old, new);
	} else {
		creat (new, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	}
}

static GKeyFile*
get_kfile (const gchar *conf)
{
	GKeyFile   *kfile = g_key_file_new ();
	const char *homedir = g_getenv ("HOME");
	char       *old = NULL;
	char       *new = NULL;

	if (!homedir)
		homedir = g_get_home_dir ();

	old = g_build_filename (homedir, OLD_PREFIX, conf, NULL);
	new = g_build_filename (homedir, NEW_PREFIX, conf, NULL);

	migrate_conf (old, new);

	g_key_file_load_from_file (kfile, new, G_KEY_FILE_NONE, NULL);
	g_key_file_set_list_separator (kfile, ' ');

	g_free (new);
	g_free (old);
	return kfile;
}

GKeyFile*
get_gpilotd_kfile ()
{
	return get_kfile ("gpilotd");
}

GKeyFile*
get_queue_kfile ()
{
	return get_kfile ("queue");
}

GKeyFile*
get_backup_kfile ()
{
	return get_kfile ("backup-conduit");
}

GKeyFile*
get_conduits_kfile (gint id)
{
	GKeyFile *kfile = NULL;
	char     *conduit = NULL;

	conduit = g_strdup_printf ("conduits%d", id);
	kfile = get_kfile (conduit);

	g_free (conduit);
	return kfile;
}

GKeyFile*
get_pilot_cache_kfile (gint id)
{
	GKeyFile *kfile = NULL;
	char     *pilot = NULL;

	pilot = g_strdup_printf ("PilotCache%d", id);
	kfile = get_kfile (pilot);

	g_free (pilot);
	return kfile;
}

static gboolean
save_config (GKeyFile    *kfile,
	     const gchar *conf)
{
	const char *homedir = g_getenv ("HOME");
        GError     *error = NULL;
        gchar      *data = NULL;
        gsize       size;
	gchar 	   *filename = NULL;

	g_return_val_if_fail (kfile, FALSE);
	g_return_val_if_fail (IS_STR_SET (conf), FALSE);

	if (!homedir)
		homedir = g_get_home_dir ();

	filename = g_build_filename (homedir, NEW_PREFIX, conf, NULL);

	if (! g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
		g_free (filename);
		g_warning ("File %s does not exsit", filename);
		return FALSE;
	}

        g_message ("Saving config to disk...");

	g_key_file_set_list_separator (kfile, ' ');
 	data = g_key_file_to_data (kfile, &size, &error);
        if (error) {
                g_warning ("Could not get config data to write to file, %s",
                           error->message);
                g_error_free (error);

                return FALSE;
        }

        g_file_set_contents (filename, data, size, &error);
        g_free (data);

        if (error) {
                g_warning ("Could not write %" G_GSIZE_FORMAT " bytes to file '%s', %s",
                           size,
                           filename,
                           error->message);
                g_free (filename);
                g_error_free (error);

                return FALSE;
        }

        g_message ("Wrote config to '%s' (%" G_GSIZE_FORMAT " bytes)",
                   filename,
                   size);

	g_free (filename);

	return TRUE;
}

gboolean
save_gpilotd_kfile (GKeyFile *kfile)
{
	return save_config (kfile, "gpilotd");
}

gboolean
save_queue_kfile (GKeyFile *kfile)
{
	return save_config (kfile, "queue");
}

gboolean
save_backup_kfile (GKeyFile *kfile)
{
	return save_config (kfile, "backup-conduit");
}

gboolean
save_conduits_kfile (GKeyFile *kfile, 
                     gint      id)
{
	gboolean  ret;
	char     *conduit = NULL;

	conduit = g_strdup_printf ("conduits%d", id);
	ret = save_config (kfile, conduit);

	g_free (conduit);
	return ret;
}

gboolean
save_pilot_cache_kfile (GKeyFile *kfile, 
                        gint      id)
{
	gboolean  ret;
	char     *pilot = NULL;

	pilot = g_strdup_printf ("PilotCache%d", id);
	ret = save_config (kfile, pilot);

	g_free (pilot);
	return ret;
}

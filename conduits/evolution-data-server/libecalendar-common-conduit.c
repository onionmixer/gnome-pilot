/*
 * Evolution calendar - ToDo Conduit
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Tom Billet <mouse256@ulyssis.org>
 *      Nathan Owens <pianocomp81@yahoo.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <string.h>

#include <libedataserver/libedataserver.h>
#include <e-pilot-util.h>
#include <pi-appinfo.h>
#include <glib.h>
#include "libecalendar-common-conduit.h"

/* Compatibility: e_cal_free_change_list was removed from modern EDS.
 * Provide a local implementation. */
void
e_cal_free_change_list (GList *list)
{
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		ECalChange *ccc = l->data;
		if (ccc) {
			if (ccc->comp)
				g_object_unref (ccc->comp);
			g_free (ccc);
		}
	}
	g_list_free (list);
}

/*make debugging possible if it's required for a conduit */
#define LOG(x)
#ifdef DEBUG_CALCONDUIT
#define LOG(x) x
#endif
#ifdef DEBUG_MEMOCONDUIT
#define LOG(x) x
#endif
#ifdef DEBUG_TODOCONDUIT
#define LOG(x) x
#endif
#ifdef DEBUG_CONDUIT
#define LOG(x) x
#endif

/*
 * Adds a category to the category app info structure (name and ID),
 * sets category->renamed[i] to true if possible to rename.
 *
 * This will be packed and written to the app info block during post_sync.
 *
 * NOTE: cat_to_add MUST be in PCHAR format. Evolution stores categories
 *       in UTF-8 format. A conversion must take place before calling
 *       this function (see e_pilot_utf8_to_pchar() in e-pilot-util.c)
 */
gint
e_pilot_add_category_if_possible(gchar *cat_to_add, struct CategoryAppInfo *category)
{
	gint i, j;
	gint retval = 0; /* 0 is the Unfiled category */
	LOG(g_message("e_pilot_add_category_if_possible\n"));

	for (i=0; i<PILOT_MAX_CATEGORIES; i++) {
		/* if strlen is 0, then the category is empty
		   the PalmOS doesn't let 0-length strings for
		   categories */
		if (strlen(category->name[i]) == 0) {
			gint cat_to_add_len;
			gint desktopUniqueID;

			cat_to_add_len = strlen(cat_to_add);
			retval = i;

			if (cat_to_add_len > 15) {
				/* Have to truncate the category name */
				cat_to_add_len = 15;
			}

			/* only 15 characters for category, 16th is
			 * '\0' can't do direct mem transfer due to
			 * declaration type
			 */
			for (j=0; j<cat_to_add_len; j++) {
				category->name[i][j] = cat_to_add[j];
			}

			for (j=cat_to_add_len; j<16; j++) {
				category->name[i][j] = '\0';
			}

			/* find a desktop id that is not in use between 128 and 255 */
			for (desktopUniqueID = 128; desktopUniqueID <= 255; desktopUniqueID++) {
				gint found = 0;
				for (j=0; j<PILOT_MAX_CATEGORIES; j++) {
					if (category->ID[i] == desktopUniqueID) {
						found = 1;
					}
				}
				if (found == 0) {
					break;
				}
				if (desktopUniqueID == 255) {
					LOG (g_warning ("*** no more categories available on PC ***"));
				}
			}
			category->ID[i] = desktopUniqueID;

			category->renamed[i] = TRUE;

			break;
		}
	}

	if (retval == 0) {
		LOG (g_warning ("*** not adding category - category list already full ***"));
	}

	return retval;
}

/*
 *conversion from an evolution category to a palm category
 */
void e_pilot_local_category_to_remote(gint * pilotCategory, ECalComponent *comp, struct CategoryAppInfo *category, const gchar *pilot_charset)
{
	GSList *c_list = NULL;
	gchar * category_string;
	gint i;
	c_list = e_cal_component_get_categories_list (comp);
	if (c_list) {
		/* list != 0, so at least 1 category is assigned */
		category_string = e_pilot_utf8_to_pchar((const gchar *)c_list->data, pilot_charset);
		if (c_list->next != 0) {
			LOG (g_message ("Note: item has more categories in evolution, first chosen"));
		}
		i=1;
		while (1) {
			if (strcmp(category_string,category->name[i]) == 0) {
				*pilotCategory = i;
				break;
			}
			i++;
			if (i == PILOT_MAX_CATEGORIES) {
				/* category not available on palm, try to create it */
				*pilotCategory = e_pilot_add_category_if_possible(category_string,category);
				break;
			}
		}
		g_slist_free_full (c_list, g_free);
		c_list = NULL;
	} else {
		*pilotCategory = 0;
	}
	/*end category*/
}

/*
 *conversion from a palm category to an evolution category
 */
void e_pilot_remote_category_to_local(gint pilotCategory, ECalComponent *comp, struct CategoryAppInfo *category, const gchar *pilot_charset)
{
	gchar *category_string = NULL;

	if (pilotCategory != 0) {
		/* pda has category assigned */
		category_string = e_pilot_utf8_from_pchar(category->name[pilotCategory], pilot_charset);

		LOG(g_message("Category: %s\n", category_string));

		/* TODO The calendar editor page and search bar are not updated until a restart of the evolution client */
		if (e_categories_exist(category_string) == FALSE) {
			/* add if it doesn't exist */
			LOG(g_message("Category created on pc\n"));
			e_categories_add(category_string, NULL, NULL, TRUE);
		}
	}

	/* store the data on in evolution */
	if (category_string == NULL) {
		/* note: this space is needed to make sure evolution clears the category */
		e_cal_component_set_categories (comp, " ");
	}
	else {

		/* Since only the first category is synced with the PDA, add the PDA's
		 * category to the beginning of the category list */
		GSList *c_list = NULL;
		GSList *newcat_in_list;
		c_list = e_cal_component_get_categories_list (comp);

		/* remove old item from list so we don't have duplicate entries */
		newcat_in_list = g_slist_find_custom(c_list, category_string, (GCompareFunc)strcmp);
		if (newcat_in_list != NULL)
		{
			c_list = g_slist_remove(c_list, newcat_in_list->data);
		}

		c_list = g_slist_prepend(c_list, category_string);
		e_cal_component_set_categories_list (comp, c_list);
		g_slist_free_full (c_list, g_free);
	}
}

/* GKeyFile-based conduit configuration storage */

static gchar *
get_conduit_config_path (void)
{
	gchar *dir = g_build_filename (g_get_user_config_dir (), "gnome-pilot", NULL);
	g_mkdir_with_parents (dir, 0755);
	g_free (dir);
	return g_build_filename (g_get_user_config_dir (), "gnome-pilot", "conduit.conf", NULL);
}

static GKeyFile *
load_conduit_keyfile (void)
{
	GKeyFile *keyfile = g_key_file_new ();
	gchar *path = get_conduit_config_path ();
	g_key_file_load_from_file (keyfile, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
	g_free (path);
	return keyfile;
}

static void
save_conduit_keyfile (GKeyFile *keyfile)
{
	gchar *path = get_conduit_config_path ();
	gchar *data = g_key_file_to_data (keyfile, NULL, NULL);
	if (data) {
		g_file_set_contents (path, data, -1, NULL);
		g_free (data);
	}
	g_free (path);
}

gboolean
e_pilot_setup_get_bool (const gchar *path, const gchar *key, gboolean def)
{
	GKeyFile *keyfile;
	gboolean res;
	GError *error = NULL;

	g_return_val_if_fail (path != NULL, def);
	g_return_val_if_fail (key != NULL, def);

	keyfile = load_conduit_keyfile ();
	res = g_key_file_get_boolean (keyfile, path, key, &error);
	if (error) {
		res = def;
		g_error_free (error);
	}
	g_key_file_free (keyfile);
	return res;
}

void
e_pilot_setup_set_bool (const gchar *path, const gchar *key, gboolean value)
{
	GKeyFile *keyfile;

	g_return_if_fail (path != NULL);
	g_return_if_fail (key != NULL);

	keyfile = load_conduit_keyfile ();
	g_key_file_set_boolean (keyfile, path, key, value);
	save_conduit_keyfile (keyfile);
	g_key_file_free (keyfile);
}

gint
e_pilot_setup_get_int (const gchar *path, const gchar *key, gint def)
{
	GKeyFile *keyfile;
	gint res;
	GError *error = NULL;

	g_return_val_if_fail (path != NULL, def);
	g_return_val_if_fail (key != NULL, def);

	keyfile = load_conduit_keyfile ();
	res = g_key_file_get_integer (keyfile, path, key, &error);
	if (error) {
		res = def;
		g_error_free (error);
	}
	g_key_file_free (keyfile);
	return res;
}

void
e_pilot_setup_set_int (const gchar *path, const gchar *key, gint value)
{
	GKeyFile *keyfile;

	g_return_if_fail (path != NULL);
	g_return_if_fail (key != NULL);

	keyfile = load_conduit_keyfile ();
	g_key_file_set_integer (keyfile, path, key, value);
	save_conduit_keyfile (keyfile);
	g_key_file_free (keyfile);
}

gchar *
e_pilot_setup_get_string (const gchar *path, const gchar *key, const gchar *def)
{
	GKeyFile *keyfile;
	gchar *res;
	GError *error = NULL;

	g_return_val_if_fail (path != NULL, g_strdup (def));
	g_return_val_if_fail (key != NULL, g_strdup (def));

	keyfile = load_conduit_keyfile ();
	res = g_key_file_get_string (keyfile, path, key, &error);
	if (error) {
		res = g_strdup (def);
		g_error_free (error);
	}
	g_key_file_free (keyfile);
	return res;
}

void
e_pilot_setup_set_string (const gchar *path, const gchar *key, const gchar *value)
{
	GKeyFile *keyfile;

	g_return_if_fail (path != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	keyfile = load_conduit_keyfile ();
	g_key_file_set_string (keyfile, path, key, value);
	save_conduit_keyfile (keyfile);
	g_key_file_free (keyfile);
}

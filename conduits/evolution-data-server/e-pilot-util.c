/*
 * Evolution Conduits - Pilot Map routines
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
 *		JP Rosevear <jpr@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <libxml/parser.h>
#include <pi-util.h>

#include "e-pilot-util.h"

gchar *
e_pilot_utf8_to_pchar (const gchar *string, const gchar *pilot_charset)
{
	gchar *pstring = NULL;
	gint res;

	if (!string)
		return NULL;

    res = convert_ToPilotChar_WithCharset ("UTF-8", string, strlen (string),
          &pstring, pilot_charset);

	if (res != 0)
		pstring = strdup (string);

	return pstring;
}

gchar *
e_pilot_utf8_from_pchar (const gchar *string, const gchar *pilot_charset)
{
	gchar *ustring = NULL;
	gint res;

	if (!string)
		return NULL;

    res = convert_FromPilotChar_WithCharset ("UTF-8", string, strlen (string),
          &ustring, pilot_charset);

	if (res != 0)
		ustring = strdup (string);

	return ustring;
}

ESource *
e_pilot_get_sync_source (ESourceRegistry *registry, const gchar *extension_name)
{
	GList *sources, *l;
	ESource *result = NULL;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	sources = e_source_registry_list_sources (registry, extension_name);
	for (l = sources; l != NULL; l = l->next) {
		ESource *source = E_SOURCE (l->data);
		if (e_source_get_enabled (source)) {
			result = g_object_ref (source);
			break;
		}
	}

	g_list_free_full (sources, g_object_unref);
	return result;
}

void
e_pilot_set_sync_source (ESourceRegistry *registry, const gchar *extension_name, ESource *source)
{
	/* In modern EDS, arbitrary properties on ESource are not supported.
	 * Sync source selection is persisted in the conduit's own config. */
}

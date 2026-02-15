/* pilot.h
 *
 * Copyright (C) 1999-2000 Free Software Foundation
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
 * Authors: Eskil Heyn Olsen
 *          Vadim Strizhevsky
 *
 */

#ifndef GPILOTD_CAPPLET_PILOT_H
#define GPILOTD_CAPPLET_PILOT_H

#include <gnome-pilot-structures.h>
#include <gnome-pilot-conduit.h>
#include <gnome-pilot-conduit-management.h>
#include <gnome-pilot-conduit-config.h>

typedef struct _ConduitState ConduitState;
struct _ConduitState {
	gchar                       *name;
	gchar                       *description;
	GPilotPilot                 *pilot;
	gchar                       *icon; 
	gboolean                    has_settings;
	gboolean                    changed;
	gboolean                    orig_enabled;
	gboolean                    enabled;
	GnomePilotConduitSyncType   sync_type;
	GnomePilotConduitSyncType   orig_sync_type;
	GnomePilotConduitSyncType   first_sync_type;
	GnomePilotConduitSyncType   orig_first_sync_type;
	GnomePilotConduitSyncType   default_sync_type;
	GnomePilotConduitManagement *management;
	GnomePilotConduitConfig     *config;
	GnomePilotConduit           *conduit;
	GtkWidget                   *settings_widget;
	GObject                     *settings_widget2;
	GList                       *valid_synctypes;
};

/* variables describing pilotd configuration state */
typedef struct _PilotState PilotState;
struct _PilotState {
	guint32      syncPCid;     /* SyncPCid for this machine */
	gint         progress_stepping; 
	GList        *pilots;
	GList        *devices;
	GHashTable   *conduits;  /* hash from pilot_id to list of 
				    ConduitSate structs */
};


PilotState *newPilotState(void);
PilotState *dupPilotState(PilotState *state);
void freePilotState(PilotState *state);

void copyPilotState(PilotState *dest, PilotState *src);
gint loadPilotState(PilotState **p);
gint savePilotState(PilotState *p);

GList *load_conduit_list (GPilotPilot *pilot);
void free_conduit_list (GList *conduits);

#endif


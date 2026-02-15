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
 *
 */

#ifndef _INTERNAL_MANAGER_H_
#define _INTERNAL_MANAGER_H_

#include <glib.h>
#include <gmodule.h>
#include <pi-source.h>
#include <pi-dlp.h>
#include "gnome-pilot-conduit.h"
#include "gnome-pilot-structures.h"
#include <time.h>

typedef struct _GnomePilotSyncStamp GnomePilotSyncStamp;
struct _GnomePilotSyncStamp {
	guint32 sync_PC_Id;
};

gboolean gpilot_initial_synchronize_operations(int pilot_socket,
					       GnomePilotSyncStamp *,
					       struct PilotUser *,
					       GList *conduit_list,
					       GList *backup_conduit_list, 
					       GList *file_conduit_list,
					       GPilotDevice *device,
					       GPilotContext *context);
gint gpilot_synchronize(int pilot_socket,
			GnomePilotSyncStamp *,
			struct PilotUser *,
			GList *conduit_list,
			GList *backup_conduit_list, 
			GList *file_conduit_list,
			GPilotContext *context);
gint gpilot_copy_to_pilot(int pilot_socket,
			  GnomePilotSyncStamp *,
			  struct PilotUser *,
			  GList *conduit_list,
			  GList *backup_conduit_list, 
			  GList *file_conduit_list,
			  GPilotContext *context);
gint gpilot_copy_from_pilot(int pilot_socket,
			    GnomePilotSyncStamp *,
			    struct PilotUser *,
			    GList *conduit_list,
			    GList *backup_conduit_list, 
			    GPilotContext *context);
gint gpilot_merge_to_pilot(int pilot_socket,
			   GnomePilotSyncStamp *,
			   struct PilotUser *,
			   GList *conduit_list,
			   GList *backup_conduit_list, 
			   GList *file_conduit_list, 
			   GPilotContext *context);
gint gpilot_merge_from_pilot(int pilot_socket,
			     GnomePilotSyncStamp *,
			     struct PilotUser *,
			     GList *conduit_list,
			     GList *backup_conduit_list, 
			     GPilotContext *context);
gint gpilot_sync_default(int pilot_socket,
			 GnomePilotSyncStamp *,
			 struct PilotUser *,
			 GList *conduit_list,
			 GList *backup_conduit_list, 
			 GList *file_conduit_list, 
			 GPilotContext *context);

void gpilot_add_log_entry(int pilot_socket,
			  gchar *entry,...);

void gpilot_remove_first_sync_settings(guint32 pilot_id,
				       gchar *config_name);

/* Given a context, a pilot, loads the conduits into three lists,
   The conduits, the backup conduits and the file install conduits.
   The latter two are typically only 1 element a list */
void gpilot_load_conduits(GPilotContext *context,
			  GPilotPilot *, 
			  GList **, 
			  GList **, 
			  GList **);

/* Deinstantiate a list of conduits */
void gpilot_unload_conduits(GList *l);

/* Restore a connected (on pfd/device) pilot (pilot) */
gboolean gpilot_start_unknown_restore (int pfd, GPilotDevice *device, GPilotPilot *pilot);

#endif /* _INTERNAL_MANAGER_H_ */

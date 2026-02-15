/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
 * Copyright (C) 1998-2001 Free Software Foundation
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gmodule.h>
#include <pi-socket.h>
#include <pi-sockaddr.h>
#include <pi-source.h>
#include <pi-sync.h>
#include <pi-dlp.h>

#include <glib/gi18n.h>

#include "gpilot-daemon.h"
#include "manager.h"
#include "queue_io.h"
#include "gnome-pilot-conduit-backup.h"
#include "gnome-pilot-conduit-file.h"
#include "gnome-pilot-conduit-standard.h"
#include "gnome-pilot-structures.h"
#include "gnome-pilot-dbinfo.h"
#include "gpilot-gui.h"
#include "gnome-pilot-config.h"

#include "gnome-pilot-conduit-management.h"
#include "gnome-pilot-conduit-config.h"

#define GPCM_LABEL "conduit_mgmt"
#define GPCC_LABEL "conduit_cfg"

/* File and Backup conduits are handled completely separately from the
   other conduits */
/*
typedef struct GnomePilotConduitManagerInfo {
	gchar *name;
	GModule *dlhandle;
	GnomePilotConduitDestroyFunc destroy_gpilot_conduit;

	gchar *config_name;
	GnomePilotConduitSyncType sync_type;
	GnomePilotConduitSyncType first_sync_type;
	gboolean slow;
} GnomePilotConduitManagerInfo;
*/
/* Typedefs. */
typedef struct {
	GnomePilotConduit *conduit;
	GPilotContext *context;
	struct PilotUser *pu;
	int pilot_socket;
        GList *conduit_list;
	GList *file_conduit_list;
        GnomePilotSyncStamp *syncstamp;
} GnomePilotInstallCarrier;

typedef gint (*iterate_func)(GnomePilotConduitStandard *, GnomePilotDBInfo *);

/* Prototypes */
static void gpilot_unload_conduit_foreach (GnomePilotConduit *conduit, gpointer unused);
static gint find_matching_conduit_compare_func (GnomePilotConduit *a,
						GnomePilotDBInfo *b);
static GnomePilotConduit *find_matching_conduit (GList *conlist, GnomePilotDBInfo *dbinfo);
static void backup_foreach (GnomePilotConduitBackup *conduit, GnomePilotDBInfo *info);
static gint iterate_dbs (gint pfd,
			 GnomePilotSyncStamp *stamp,
			 struct PilotUser *pu,
			 GList *conlist,
			 GList *bconlist,
			 iterate_func iter,
			 GPilotContext *context);
static gint conduit_synchronize (GnomePilotConduitStandard *conduit,
				 GnomePilotDBInfo *dbinfo);
static gint conduit_copy_to_pilot (GnomePilotConduitStandard *conduit,
				   GnomePilotDBInfo *dbinfo);
static gint conduit_copy_from_pilot (GnomePilotConduitStandard *conduit,
				     GnomePilotDBInfo *dbinfo);
static gint conduit_sync_default (GnomePilotConduitStandard *conduit,
			     GnomePilotDBInfo *dbinfo);
static void gpilot_manager_save_databases (GPilotPilot *dbinfo, GSList *databases);
/* This is defined in gpilotd.  But we need to use it... */
gchar *pilot_name_from_id (guint32 id, GPilotContext *context);

/**************************************************************************/
/* These are various match methods, used to do g_list_find's              */

static gint
find_matching_conduit_compare_func (GnomePilotConduit *conduit,
				    GnomePilotDBInfo *dbinfo)
{
	if (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit)) {
		if ((gnome_pilot_conduit_standard_get_creator_id (GNOME_PILOT_CONDUIT_STANDARD (conduit)) ==
		     PI_DBINFO (dbinfo)->creator) &&
		    (strcmp (gnome_pilot_conduit_standard_get_db_name (GNOME_PILOT_CONDUIT_STANDARD (conduit)),
			    PI_DBINFO (dbinfo)->name) == 0)) {
			return 0;
		}
	}
	return -1;
}

static GnomePilotConduit *
find_matching_conduit (GList *conlist,
		       GnomePilotDBInfo *dbinfo)
{
	GList *elem;
	elem = g_list_find_custom (conlist,
				  dbinfo,
				  (GCompareFunc) find_matching_conduit_compare_func);
	if (elem)
		return GNOME_PILOT_CONDUIT (elem->data);
	else
		return NULL;
}

static gint 
find_matching_conduit_compare_func_name (GnomePilotConduit *conduit,
					gchar *name) 
{
	if (GNOME_IS_PILOT_CONDUIT (conduit)) {
		if (strcmp (gnome_pilot_conduit_get_name (conduit),
			    name)==0) {
			return 0;
		}
	}
	return -1;
}

static GnomePilotConduit*
find_matching_conduit_name (GList *conlist, gchar *name)
{
	GList *elem;
	elem=g_list_find_custom (conlist, name,
				 (GCompareFunc)find_matching_conduit_compare_func_name);
	if (elem)
		return GNOME_PILOT_CONDUIT (elem->data);
	else
		return NULL;
}

/**************************************************************************/
/* Here comes conduit signal handler, used to call methods from 
   gpilot-daemon                                                      */

typedef struct {
	GnomePilotInstallCarrier *carrier;
	GnomePilotConduitFile *file_conduit;
	gchar *pilot_name;
} GnomePilotBackupFuncCarrier;

static gint
gnome_pilot_conduit_backup_func (gchar *filename,
				 gint db,
				 gint total_dbs,
				 GnomePilotBackupFuncCarrier *carrier) {
	dbus_notify_overall_progress (carrier->pilot_name, db, total_dbs);
	
	g_message ("gnome_pilot_conduit_backup_func (%s)", filename);

	return gnome_pilot_conduit_file_install_db (carrier->file_conduit,
						    carrier->carrier->pilot_socket,
						    filename,
						    FALSE);
}

static void 
conduit_progress_callback (GnomePilotConduit *conduit, 
			   gint total,
			   gint current,
			   gpointer pilot_id) {
	dbus_notify_conduit_progress ((gchar*)pilot_id,
				       conduit,
				       current,
				       total);
}

static void 
conduit_message_callback (GnomePilotConduit *conduit, 
			  gchar *message,
			  gpointer pilot_id) {
	dbus_notify_conduit_message ((gchar*)pilot_id,
				      conduit,
				      message);
}

static void 
conduit_error_callback (GnomePilotConduit *conduit, 
			gchar *message,
			gpointer pilot_id) {
	dbus_notify_conduit_error ((gchar*)pilot_id,
				    conduit,
				    message);
}

/* This toggles the callbacks for signal. Call with set=TRUE to 
   register the callbacks, and FALSE to remove them.
   Remember to match these two calls!
   NOTE: Not reentrant! lock when set, unlock when unset.
   could potientially be reentrant since the pilot-name
   would be unique even for multicradle sync
   FIXME: yech, actually this is quite ugly
*/
static void 
set_callbacks (gboolean set, GnomePilotConduit *conduit, gchar *pilot_name) 
{
	static guint progress_h = 0,
		     error_h    = 0,
		     message_h  = 0;
	if (set==TRUE) {
		if (progress_h != 0) g_warning ("Internal inconsistency, see %s:%d",
					       __FILE__,__LINE__);
		
		progress_h = gnome_pilot_conduit_connect__progress (conduit, conduit_progress_callback,
								    pilot_name);
		error_h = gnome_pilot_conduit_connect__error  (conduit, conduit_error_callback,
							       pilot_name);
		message_h = gnome_pilot_conduit_connect__message (conduit, conduit_message_callback,
								  pilot_name);
	} else {
		if (progress_h == 0) {
			/* We should never have set==FALSE but no progress_h(andler)
			   set. This can be caused by faulty conduits that do not
			   load properly */
			g_warning ("Internal inconsistency, see %s:%d",
				   __FILE__,__LINE__);
		}
		
		g_signal_handler_disconnect (G_OBJECT (conduit), progress_h);
		g_signal_handler_disconnect (G_OBJECT (conduit), error_h);
		g_signal_handler_disconnect (G_OBJECT (conduit), message_h);
		progress_h = 0;
		error_h    = 0;
		message_h  = 0;
	}
}

/**************************************************************************/

/* Carrier structure for backup conduits restore callback */

static void
backup_foreach (GnomePilotConduitBackup *conduit, GnomePilotDBInfo *info)
{
	int result;
	
	if (GNOME_IS_PILOT_CONDUIT_BACKUP (conduit) == FALSE) {
		g_error (_("non-backup conduit in backup conduit list"));
		return;
	}
	g_assert (info != NULL);
	
	set_callbacks (TRUE, GNOME_PILOT_CONDUIT (conduit), info->pilotInfo->name);
	result = gnome_pilot_conduit_backup_backup (conduit, info);	
	if (result == 0) {
		gpilot_add_log_entry (info->pilot_socket, _("%s backed up\n"), PI_DBINFO (info)->name);
	} else if (result < 0) {
		gpilot_add_log_entry (info->pilot_socket, _("%s backup failed\n"), PI_DBINFO (info)->name);
	} else if (result > 0) {
		/* db not modified, not backup up */
	}
	set_callbacks (FALSE, GNOME_PILOT_CONDUIT (conduit), info->pilotInfo->name);
}

static gint
iterate_dbs (gint pfd,
	     GnomePilotSyncStamp *stamp,
	     struct PilotUser *pu,
	     GList *conlist,
	     GList *bconlist,
	     iterate_func iter,
	     GPilotContext *context)
{
	GList *dbs = NULL, *iterator;
	GnomePilotConduit *conduit = NULL;
	GSList *db_list = NULL;
	GPilotPilot *pilot_info;
	int error = 0;
	int index = 0;
	int result;

	pilot_info = gpilot_find_pilot_by_id (pu->userID, context->pilots);

	dbus_notify_daemon_message (pilot_info->name, NULL, _("Collecting synchronization info..."));

	while (1) {
		GnomePilotDBInfo *dbinfo;
		pi_buffer_t *pi_buf;
		dbinfo = g_new0 (GnomePilotDBInfo, 1);

		pi_buf = pi_buffer_new (sizeof (struct DBInfo));
		result = dlp_ReadDBList (pfd, 0, dlpDBListRAM, index, pi_buf);
		/* load next dbinfo block */
		if (result < 0) {
			/* is <0, there are no more databases, break
                           out so we can save the list */
			g_free (dbinfo);
			break;
		}

		memcpy (dbinfo, pi_buf->data, sizeof (struct DBInfo));
		pi_buffer_free (pi_buf);
		index = PI_DBINFO (dbinfo)->index + 1;

		dbs = g_list_append (dbs, dbinfo);
	}

	index = 1;
	for (iterator = dbs; iterator; iterator = g_list_next (iterator)) {
		GnomePilotDBInfo *dbinfo = GNOME_PILOT_DBINFO (iterator->data);

		if (dlp_OpenConduit (pfd) < 0) {
			g_warning ("Unable to open conduit!");
			error = -1;
			break;
		}

		db_list = g_slist_append (db_list, g_strdup (PI_DBINFO (dbinfo)->name));

		if (0) {
			char creat[4];
			memcpy (creat, (void*)&PI_DBINFO (dbinfo)->creator, 4);
			g_message ("DB: %20s: Exclude=%3s, Backup=%3s Flags=0x%8.8x Creator=0x%lx (%c%c%c%c) Backupdate = %ld modifydate = %ld, %d/%d",
				   PI_DBINFO (dbinfo)->name,
				   (PI_DBINFO (dbinfo)->miscFlags & dlpDBMiscFlagExcludeFromSync)?"yes":"no",
				   (PI_DBINFO (dbinfo)->flags & dlpDBFlagBackup)?"yes":"no",
				   PI_DBINFO (dbinfo)->flags,
				   PI_DBINFO (dbinfo)->creator, creat[3], creat[2], creat[1], creat[0],
				   PI_DBINFO (dbinfo)->backupDate,
				   PI_DBINFO (dbinfo)->modifyDate,
				   index, g_list_length (dbs));
		}

		/* check if the base is marked as being excluded from sync */
		if (!(PI_DBINFO (dbinfo)->miscFlags & dlpDBMiscFlagExcludeFromSync)) {
			dbinfo->pu = pu;
			dbinfo->pilot_socket = pfd;
			dbinfo->manager_data = (void *) stamp;
			dbinfo->pilotInfo = pilot_info;

			conduit = find_matching_conduit (conlist, dbinfo);

			if (conduit) {
				/* Now call the iter method on the conduit, after setting up
				   the signals. The signals aren't set before, since we 
				   want to pass dbinfo as the userdata, and we don't do it
				   in "iter", since it's a parameterized funtion */				
				set_callbacks (TRUE, GNOME_PILOT_CONDUIT (conduit), pilot_info->name);
				error = iter (GNOME_PILOT_CONDUIT_STANDARD (conduit), dbinfo);
				set_callbacks (FALSE, GNOME_PILOT_CONDUIT (conduit), pilot_info->name);
			}

			dbus_notify_overall_progress (pilot_info->name, index, g_list_length (dbs)); 

			/*
			  It seems pretty erratic which has this and which hasn't,
			  so I'm not doing this check.
			if (PI_DBINFO (dbinfo)->miscFlags & dlpDBFlagBackup)
			*/
				g_list_foreach (bconlist,
						(GFunc) backup_foreach,
						dbinfo);

		} else {
			LOG (("Base %s is to be ignored by sync", PI_DBINFO (dbinfo)->name));
			continue;
		}
		g_free (dbinfo); 
		index++;
	}
	g_list_free (dbs);

	gpilot_manager_save_databases (pilot_info, db_list);
	return error;
}

static void
write_sync_type_line_to_pilot (GnomePilotDBInfo *dbinfo,
			       GnomePilotConduit *conduit,
			       char *format)
{
	char *conduit_name;
	conduit_name = gnome_pilot_conduit_get_name (conduit);
	if (conduit_name!=NULL) {
		gpilot_add_log_entry (dbinfo->pilot_socket, format, conduit_name);
	}
	g_free (conduit_name);	
}

static gint
conduit_synchronize (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	int err;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);

	write_sync_type_line_to_pilot (dbinfo, GNOME_PILOT_CONDUIT (conduit), _("Synchronizing %s\n"));

	dbus_notify_conduit_start (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit),
				   GnomePilotConduitSyncTypeSynchronize);
	err = gnome_pilot_conduit_standard_synchronize (conduit, dbinfo);
	dbus_notify_conduit_end (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit));
	return err;
}

static gint
conduit_copy_to_pilot (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	int err;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);

	write_sync_type_line_to_pilot (dbinfo, GNOME_PILOT_CONDUIT (conduit), _("Copy to PDA %s\n"));

	dbus_notify_conduit_start (dbinfo->pilotInfo->name,
				    GNOME_PILOT_CONDUIT (conduit),
				    GnomePilotConduitSyncTypeCopyToPilot);
	err = gnome_pilot_conduit_standard_copy_to_pilot (conduit, dbinfo);
	dbus_notify_conduit_end (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit));
	return err;
}

static gint
conduit_copy_from_pilot (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	int err;
	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);

	write_sync_type_line_to_pilot (dbinfo, GNOME_PILOT_CONDUIT (conduit), _("Copy from PDA %s\n"));

	dbus_notify_conduit_start (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit),
				   GnomePilotConduitSyncTypeCopyFromPilot);
	err = gnome_pilot_conduit_standard_copy_from_pilot (conduit, dbinfo);
	dbus_notify_conduit_end (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit));
	return err;
}

static gint
conduit_merge_to_pilot (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	int err;
	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);

	write_sync_type_line_to_pilot (dbinfo, GNOME_PILOT_CONDUIT (conduit), _("Merge to PDA %s\n"));

	dbus_notify_conduit_start (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit),
				   GnomePilotConduitSyncTypeMergeToPilot);
	err = gnome_pilot_conduit_standard_merge_to_pilot (conduit, dbinfo);
	dbus_notify_conduit_end (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit));
	return err;
}

static gint
conduit_merge_from_pilot (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	int err;
	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);

	write_sync_type_line_to_pilot (dbinfo, GNOME_PILOT_CONDUIT (conduit), _("Merge from PDA %s\n"));

	dbus_notify_conduit_start (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit),
				   GnomePilotConduitSyncTypeMergeFromPilot);
	err = gnome_pilot_conduit_standard_merge_from_pilot (conduit, dbinfo);
	dbus_notify_conduit_end (dbinfo->pilotInfo->name,
				   GNOME_PILOT_CONDUIT (conduit));
	return err;
}

static gint
conduit_sync_default (GnomePilotConduitStandard *conduit, GnomePilotDBInfo *dbinfo)
{
	GnomePilotConduitConfig *conduit_config;
	GnomePilotConduitManagement *manager;
	GnomePilotConduitSyncType action;

	g_return_val_if_fail (conduit != NULL, -1);
	g_return_val_if_fail (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit), -1);
	g_return_val_if_fail (dbinfo!=NULL, -1);
	g_return_val_if_fail (dbinfo->pu!=NULL, -1);

	conduit_config = g_object_get_data (G_OBJECT (conduit), GPCC_LABEL);
	manager = g_object_get_data (G_OBJECT (conduit), GPCM_LABEL);
	g_return_val_if_fail (conduit_config!=NULL,-1);
	if (conduit_config->first_sync_type != GnomePilotConduitSyncTypeNotSet) {
		const char *conduit_name = gnome_pilot_conduit_management_get_name (manager);
		g_message (_("Conduit %s's first synchronization..."), conduit_name);

		gnome_pilot_conduit_config_remove_first_sync (conduit_config);
		action = conduit_config->first_sync_type;
		if (conduit_config->first_slow==TRUE) {
			gnome_pilot_conduit_standard_set_slow (conduit, TRUE);
		}
	} else {
		action = conduit_config->sync_type;
	}
		
	switch (action) {
	case GnomePilotConduitSyncTypeSynchronize:
	case GnomePilotConduitSyncTypeCustom:
		return conduit_synchronize (conduit, dbinfo);
	case GnomePilotConduitSyncTypeCopyToPilot:
		return conduit_copy_to_pilot (conduit, dbinfo);
	case GnomePilotConduitSyncTypeCopyFromPilot:
		return conduit_copy_from_pilot (conduit, dbinfo);
	case GnomePilotConduitSyncTypeMergeToPilot:
		return conduit_merge_to_pilot (conduit, dbinfo);
	case GnomePilotConduitSyncTypeMergeFromPilot:
		return conduit_merge_from_pilot (conduit, dbinfo);
	default:
		g_warning (_("unknown syncing action (%d = %s).\n"), action,
			   gnome_pilot_conduit_sync_type_int_to_str (action));
		return -1;
	}
}

static void
install_db_foreach (GPilotRequest *req, GnomePilotInstallCarrier *carrier) {
	gchar *pilot_name;
	int err;

	pilot_name = pilot_name_from_id (carrier->pu->userID, carrier->context);
	set_callbacks (TRUE, carrier->conduit, pilot_name);
	err = gnome_pilot_conduit_file_install_db (GNOME_PILOT_CONDUIT_FILE (carrier->conduit),
						   carrier->pilot_socket,
						   req->parameters.install.filename,
						   TRUE);
	set_callbacks (FALSE, carrier->conduit, pilot_name);

	g_free (pilot_name);
	dbus_notify_completion (&req);
}

static void
install_foreach (GnomePilotConduit *conduit,
		 GnomePilotInstallCarrier *info)
{
	GList *request_list;

	g_return_if_fail (conduit != NULL);
	g_return_if_fail (info != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT_FILE (conduit));

	info->conduit = conduit;
	request_list = gpc_queue_load_requests (info->pu->userID, GREQ_INSTALL, FALSE);
	g_message ("Pilot has %d entries in file install queue", g_list_length (request_list));
	g_list_foreach (request_list, (GFunc)install_db_foreach, info);
	g_list_free (request_list);
}

static void
do_restore_foreach (GPilotRequest *req, GnomePilotInstallCarrier *carrier)
{
	GList *iterator;
	gchar *pilot_name;
	
	g_assert (carrier!=NULL);

	pilot_name = pilot_name_from_id (carrier->pu->userID, carrier->context);
	/* FIXME: if carrier.conduit_list is NULL, the user hasn't
	  enabled any file conduits. The dbus restore call should
	  throw an exception or something.  */
	iterator = carrier->conduit_list;
	while ( iterator != NULL ) {
		GList *iterator_two = carrier->file_conduit_list;
		GnomePilotConduitBackup *backup_conduit = GNOME_PILOT_CONDUIT_BACKUP (iterator->data);
		
		while (iterator_two) {
			GnomePilotBackupFuncCarrier backup_carrier;
			GnomePilotConduitFile *file_conduit = GNOME_PILOT_CONDUIT_FILE (iterator_two->data);
			backup_carrier.carrier = carrier;
			backup_carrier.file_conduit = file_conduit;
			backup_carrier.pilot_name = pilot_name;

			set_callbacks (TRUE, GNOME_PILOT_CONDUIT (file_conduit), pilot_name);
			gnome_pilot_conduit_backup_restore (backup_conduit, 
							    carrier->pilot_socket,
							    req->parameters.restore.directory,
							    (GnomePilotConduitBackupRestore)gnome_pilot_conduit_backup_func,
							    &backup_carrier);
			set_callbacks (FALSE, GNOME_PILOT_CONDUIT (file_conduit), pilot_name);
			
			iterator_two = g_list_next (iterator_two);
		}
		iterator = g_list_next (iterator);
	}

	g_free (pilot_name);
	dbus_notify_completion (&req);
}

static void
gpilot_unload_conduit_foreach (GnomePilotConduit *conduit,
			       gpointer unused)
{
	GnomePilotConduitManagement *manager;
	g_return_if_fail (conduit != NULL);
	g_return_if_fail (GNOME_IS_PILOT_CONDUIT (conduit));
	
	manager = g_object_get_data (G_OBJECT (conduit), GPCM_LABEL);
	g_assert (manager!=NULL);
	/* FIXME: _destroy_conduit doesn't g_module_close... is this an issue ? 
	 note: the shlib loader in gnome_pilot_conduit_management now closes
	after loading the methods. */
	gnome_pilot_conduit_management_destroy_conduit (manager,&conduit);
	gnome_pilot_conduit_management_destroy (manager);
}


void
gpilot_load_conduits (GPilotContext *context,
		      GPilotPilot *pilot,
		      GList **clist,
		      GList **blist,
		      GList **flist)
{
	int i;
	gsize cnt;
	GnomePilotConduit *conduit;
	guint32 pilotId;
	gchar **conduit_name;
	GnomePilotConduitManagement *manager;
	GnomePilotConduitConfig *conduit_config;
	GKeyFile *kfile;

	pilotId = pilot->pilot_id;

	*clist = NULL;
	*blist = NULL;
	*flist = NULL;

	/* Read the conduit configuration */
	kfile = get_conduits_kfile (pilot->pilot_id);
	conduit_name = g_key_file_get_string_list (kfile, "General", "conduits", &cnt, NULL);
	g_key_file_free (kfile);

	g_message (_("Instantiating %d conduits..."), (gint) cnt);
	for (i = 0;i<cnt ;i++) {
		gint err;
		manager = gnome_pilot_conduit_management_new (conduit_name[i], GNOME_PILOT_CONDUIT_MGMT_ID);
		if (manager == NULL) {
			g_message (_("Unknown conduit \"%s\" in configure!"), conduit_name[i]);			
			gpilot_gui_warning_dialog (_("Unknown conduit \"%s\" in configure!"), conduit_name[i]); 
			continue;
		}
		conduit_config = gnome_pilot_conduit_config_new (manager, pilot->pilot_id);
		gnome_pilot_conduit_config_load_config (conduit_config);
		err = gnome_pilot_conduit_management_instantiate_conduit (manager,
									  pilot,
									  &conduit);
		if (err != GNOME_PILOT_CONDUIT_MGMT_OK || conduit == NULL) {
			g_message (_("Loading conduit \"%s\" failed!"), conduit_name[i]);
			gpilot_gui_warning_dialog (_("Loading conduit \"%s\" failed!\n"), conduit_name[i]);
			gnome_pilot_conduit_config_destroy (conduit_config);
			gnome_pilot_conduit_management_destroy (manager);
			continue;
		}
		
		g_object_set (G_OBJECT (conduit),
				GNOME_PILOT_CONDUIT_PROP_PROGRESS_STEPPING (context->progress_stepping),
				NULL);
		g_object_set_data (G_OBJECT (conduit), GPCM_LABEL, manager);
		g_object_set_data (G_OBJECT (conduit), GPCC_LABEL, conduit_config);

		if (GNOME_IS_PILOT_CONDUIT_BACKUP (conduit)) {
			*blist = g_list_append (*blist, conduit);
		} else if (GNOME_IS_PILOT_CONDUIT_FILE (conduit)) {
			*flist = g_list_append (*flist, conduit);
		} else if (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit)) {
			*clist = g_list_append (*clist, conduit);
		} else {
			g_warning ("Error in conduits definition file for pilot %d, %s is not a valid conduit",
				  pilot->number,
				  conduit_name[i]);
		}

		g_free (conduit_name[i]);
	}
	g_free (conduit_name);
	g_message ("Instantiated %d backup conduits, %d file conduits, %d other conduits",
		   g_list_length (*blist), g_list_length (*flist), g_list_length (*clist));

}

void
gpilot_unload_conduits (GList *list)
{
	g_list_foreach (list, (GFunc) gpilot_unload_conduit_foreach, NULL);
	g_list_free (list);
}

/* 
 * Start restoring a pilot profile, using the given (open hopefully)
 * filedescriptor, given a device (in case we need it again) and the
 * pilot info structure
 */

gboolean
gpilot_start_unknown_restore (int pfd, 
			      GPilotDevice *device,
			      GPilotPilot *pilot)
{
	GnomePilotConduitManagement *gpcmb, *gpcmf;
	GnomePilotConduit *b_conduit, *f_conduit;
	int err1, err2;
	gboolean result = TRUE;

	gpcmb = gnome_pilot_conduit_management_new ("gpbackup1", GNOME_PILOT_CONDUIT_MGMT_ID);
	gpcmf = gnome_pilot_conduit_management_new ("gpfile1", GNOME_PILOT_CONDUIT_MGMT_ID);
	err1 = gnome_pilot_conduit_management_instantiate_conduit (gpcmb,
								   pilot,
								   &b_conduit);
	err2 = gnome_pilot_conduit_management_instantiate_conduit (gpcmf,
								   pilot,
								   &f_conduit);
		
	if (err1 == GNOME_PILOT_CONDUIT_MGMT_OK && 
	    err2 == GNOME_PILOT_CONDUIT_MGMT_OK &&
	    GNOME_IS_PILOT_CONDUIT_BACKUP (b_conduit) &&
	    GNOME_IS_PILOT_CONDUIT_FILE (f_conduit)) {
		GnomePilotBackupFuncCarrier backup_carrier;
		backup_carrier.carrier = g_new0 (GnomePilotInstallCarrier, 1);
		backup_carrier.carrier->pilot_socket = pfd;
		backup_carrier.pilot_name = pilot->name;
		backup_carrier.file_conduit = GNOME_PILOT_CONDUIT_FILE (f_conduit);

		set_callbacks (TRUE, GNOME_PILOT_CONDUIT (f_conduit), pilot->name);
		gnome_pilot_conduit_backup_restore (GNOME_PILOT_CONDUIT_BACKUP (b_conduit),
						    pfd, 
						    NULL,
						    (GnomePilotConduitBackupRestore)gnome_pilot_conduit_backup_func,
						    &backup_carrier);
		set_callbacks (FALSE, GNOME_PILOT_CONDUIT (f_conduit), pilot->name);

		g_free (backup_carrier.carrier);
	} else {
		result = FALSE;
	}
	
	if (result) {
		struct PilotUser pu;
		char *tmp;
		tmp = g_strdup_printf (_("Setting id/owner to %d/%s..."), pilot->pilot_id, pilot->pilot_username);
		dbus_notify_daemon_message (pilot->name, NULL, tmp);
		g_snprintf (pu.username, 127, "%s", pilot->pilot_username);
		pu.userID = pilot->pilot_id;
		dlp_WriteUserInfo (pfd, &pu);
		g_free (tmp);
	}
							    
	return result;
}

static void 
run_conduit_sync_foreach (GPilotRequest *req,
			 GnomePilotInstallCarrier *carrier)
{
	GnomePilotConduit *conduit;
	GList *clist,*blist,*flist;
	int err;

	g_return_if_fail (carrier!=NULL);

	err = 0;
	clist = NULL;
	blist = NULL;
	flist = NULL;
	conduit = NULL;

	conduit = find_matching_conduit_name (carrier->conduit_list,
					      req->parameters.conduit.name);
	if (conduit==NULL) {
		/* FIXME: remove the bloody request, or the user will get this
		   message every time */
		g_warning (_("Conduit %s requested, but not found.  Is it enabled?"),
			   req->parameters.conduit.name);
	} else {
		if (GNOME_IS_PILOT_CONDUIT_BACKUP (conduit)) {
			blist = g_list_append (blist, conduit);
		} else if (GNOME_IS_PILOT_CONDUIT_STANDARD (conduit)) {
			clist = g_list_append (clist, conduit);
		} else if (GNOME_IS_PILOT_CONDUIT_FILE (conduit)) {
			flist = g_list_append (flist, conduit);
		} else {
			g_error (_("Conduit %s cannot be requested"),
				req->parameters.conduit.name);
		}

		if (flist) {
			g_list_foreach (flist, (GFunc)install_foreach, carrier);
		} else {
			err= iterate_dbs (carrier->pilot_socket,
					  carrier->syncstamp,
					  carrier->pu,
					  clist,
					  blist,
					  conduit_sync_default,
					  carrier->context); 
		}
	}
	dbus_notify_completion (&req);
}

gboolean gpilot_initial_synchronize_operations (int pilot_socket,
					       GnomePilotSyncStamp *stamp,
					       struct PilotUser *pu,
					       GList *conduit_list,
					       GList *backup_conduit_list,
					       GList *file_conduit_list,
					       GPilotDevice *device,
					       GPilotContext *context)
{
	GList *request_list;
	gboolean do_normal_sync_stuff;
	GnomePilotInstallCarrier carrier;

	carrier.pu = pu;
	carrier.pilot_socket = pilot_socket;
	carrier.context = context;

	/* If this variable is true, run all the normal conduit stuff in
	   conduit_list, backup_conduit_list and file_conduit_list in the end.
	   If false (getsysinfo, restore and/or single conduit run), dont
	   use the normal conduits */
	do_normal_sync_stuff = TRUE;

	/* elements in request_list freed by gpc_request_purge calls
           in do_restore_foreach */
	request_list = gpc_queue_load_requests (pu->userID, GREQ_RESTORE, FALSE);
	g_message ("Pilot has %d entries in restore queue", g_list_length (request_list));
	if (g_list_length (request_list)) {
		do_normal_sync_stuff = FALSE;
		carrier.conduit_list = g_list_copy (backup_conduit_list);
		carrier.file_conduit_list = g_list_copy (file_conduit_list);
		g_list_foreach (request_list,(GFunc)do_restore_foreach,&carrier);
	}

	/* elements in request_list freed by gpc_request_purge calls
           in run_conduit_sync_foreach */
	request_list = gpc_queue_load_requests (pu->userID, GREQ_CONDUIT, FALSE);
	g_message ("Pilot has %d entries in conduit queue", g_list_length (request_list));
	if (g_list_length (request_list)) {
		carrier.conduit_list = g_list_copy (conduit_list);
		carrier.conduit_list = g_list_concat (carrier.conduit_list, g_list_copy (backup_conduit_list));
		carrier.conduit_list = g_list_concat (carrier.conduit_list, g_list_copy (file_conduit_list));
		do_normal_sync_stuff = FALSE;
		g_list_foreach (request_list,(GFunc)run_conduit_sync_foreach,&carrier);
		g_list_free (carrier.conduit_list);

	}

	return do_normal_sync_stuff;
}

gint
gpilot_synchronize (int pilot_socket,
		   GnomePilotSyncStamp *stamp,
		   struct PilotUser *pu,
		   GList *conduit_list,
		   GList *backup_conduit_list,
		   GList *file_conduit_list,
		   GPilotContext *context)
{
	gint error;
	GnomePilotInstallCarrier carrier;

	carrier.pu = pu;
	carrier.pilot_socket = pilot_socket;
	carrier.context = context;

	/* now run through all the file install requests */
	g_list_foreach (file_conduit_list, (GFunc)install_foreach, &carrier);

	/* and then iterate over all conduits */
	error = iterate_dbs (pilot_socket,
			     stamp,
			     pu,
			     conduit_list,
			     backup_conduit_list,
			     conduit_synchronize,
			     context);

	/* FIXME: empty queue for pilot here */

	return error;
}

gint 
gpilot_copy_to_pilot (int pilot_socket, 
		     GnomePilotSyncStamp *stamp,
		     struct PilotUser *pu,
		     GList *conduit_list,
		     GList *backup_conduit_list,
		     GList *file_conduit_list,
		     GPilotContext *context)
{
	gint error;
	GnomePilotInstallCarrier carrier;

	carrier.pu = pu;
	carrier.pilot_socket = pilot_socket;
	carrier.context = context;

	g_list_foreach (file_conduit_list, (GFunc)install_foreach, &carrier);
	
	error = iterate_dbs (pilot_socket, stamp, pu,
			     conduit_list,
			     backup_conduit_list,
			     conduit_copy_to_pilot,
			     context);
	return error;
}

gint 
gpilot_copy_from_pilot (int pilot_socket, 
		       GnomePilotSyncStamp *stamp,
		       struct PilotUser *pu,
		       GList *conduit_list,
		       GList *backup_conduit_list,
		       GPilotContext *context)
{
	gint error;
	error = iterate_dbs (pilot_socket, stamp, pu,
			    conduit_list,
			    backup_conduit_list,
			    conduit_copy_from_pilot,
			    context);
	return error;
}

gint 
gpilot_merge_to_pilot (int pilot_socket, 
		      GnomePilotSyncStamp *stamp,
		      struct PilotUser *pu,
		      GList *conduit_list,
		      GList *backup_conduit_list, 
		      GList *file_conduit_list,
		      GPilotContext *context)
{
	gint error;
	GnomePilotInstallCarrier carrier;

	carrier.pu = pu;
	carrier.pilot_socket = pilot_socket;
	carrier.context = context;

	g_list_foreach (file_conduit_list, (GFunc)install_foreach, &carrier);

	error = iterate_dbs (pilot_socket, stamp, pu,
			     conduit_list,
			     backup_conduit_list,
			     conduit_merge_to_pilot,
			     context);

	return error;
}

gint 
gpilot_merge_from_pilot (int pilot_socket, 
			GnomePilotSyncStamp *stamp,
			struct PilotUser *pu,
			GList *conduit_list,
			GList *backup_conduit_list,
			GPilotContext *context)
{
	gint error;
	error = iterate_dbs (pilot_socket, stamp, pu, conduit_list, backup_conduit_list,
			  conduit_merge_from_pilot, context);
	return error;
}

gint 
gpilot_sync_default (int pilot_socket, GnomePilotSyncStamp *stamp,
		    struct PilotUser *pu,
		    GList *conduit_list,
		    GList *backup_conduit_list,
		    GList *file_conduit_list,
		    GPilotContext *context)
{
	gint error;
	GnomePilotInstallCarrier carrier;

	carrier.pu = pu;
	carrier.pilot_socket = pilot_socket;
	carrier.context = context;
	g_list_foreach (file_conduit_list, (GFunc)install_foreach, &carrier);

	error=iterate_dbs (pilot_socket, 
			   stamp, 
			   pu, 
			   conduit_list, 
			   backup_conduit_list,
			   conduit_sync_default, 
			   context);
	return error;
}

void 
gpilot_add_log_entry (int pilot_socket,
		     gchar *entry, ...) 
{
	gchar *e,*f;

	va_list ap;

	va_start (ap, entry);
	e = g_strdup_vprintf (entry, ap);
	f = g_strdup_printf ("%s ", e);
	dlp_AddSyncLogEntry (pilot_socket, f);
	g_free (e);
	g_free (f);
}

/**
   save the list of databases this pilot has
 */
void 
gpilot_manager_save_databases (GPilotPilot *pilot_info, 
			      GSList *databases) {
	char **charlist;
	int cnt=0;
	GSList *ptr;
	GKeyFile *kfile;

	charlist = g_new0(char*, g_slist_length (databases)+1);

	for (ptr = databases; ptr != NULL; ptr = ptr->next) {
		charlist[cnt] = ptr->data;
		cnt++;
	}

	kfile = get_pilot_cache_kfile (pilot_info->pilot_id);
	g_key_file_set_string_list (kfile, "Databases", "databases",
				(const char**)charlist,
				g_slist_length (databases));
	save_pilot_cache_kfile (kfile, pilot_info->pilot_id);
	g_key_file_free (kfile);

	/* FIXME: is this okay ? */
	g_slist_foreach (databases,(GFunc)g_free, NULL);
	g_free (charlist);
	g_slist_free (databases);
}

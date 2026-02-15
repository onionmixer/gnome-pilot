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
 */

/* $Id$ */

#include <glib.h>
#include <glib/gi18n.h>
#include <pi-source.h>
#include <pi-socket.h>
#include <stdio.h>
#include <pi-file.h>
#include <pi-dlp.h>
#include <pi-version.h>
#include <pi-sync.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <unistd.h>
#include <pwd.h>
#include <signal.h>

#include <gnome-pilot-conduit-file.h>

/* pilot stuff begin */
#include <stdio.h>
#include <time.h>
#ifdef __EMX__
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <signal.h>
#include <utime.h>
#include "pi-source.h"
#include "pi-socket.h"
#include "pi-file.h"
#include "pi-dlp.h"

#define pi_mktag(c1,c2,c3,c4) (((c1)<<24)|((c2)<<16)|((c3)<<8)|(c4))
#define MAXDBS 256
#define cardno 0 

/* pilot stuff end */

GnomePilotConduit *conduit_get_gpilot_conduit (guint32 pilotId);
GnomePilotConduit *conduit_load_gpilot_conduit (GPilotPilot *pilot);
void conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit);

typedef struct file_db {
        gchar name[256];
        gchar dbname[256];
        gint flags;
        gulong creator;
        gulong type;
        gint maxblock;
} file_db;

#ifdef DEBUG_REC_IO
static char*
pi_unmktag (long type) {
	static char tag[5];
	tag[0] = (type >> 24) & 0xff;
	tag[1] = (type >> 16) & 0xff;
	tag[2] = (type >> 8) & 0xff;
	tag[3] = type & 0xff;
	tag[4] = 0;
	return tag;
}
#endif

/*
  Warning, Danger, Alert, Hot Mocha!

  A lot of the code here is copied directly from pilot-link/libsock/pi-file.c,
  since I need to get feedback during progress for Great User Experience (what
  would Arlo Rose call this ?)

  So anyhoo, occasionally, go through said file, and check to see if there are
  any new special cases (like 'g''r''a''f') that needs to be added
 */
static gint
gnome_real_pilot_conduit_file_install_db (GnomePilotConduitFile *conduit,
					  int psock,
					  gchar *src_file,
					  gboolean rm,
					  gpointer unused)
{
	int index = 0;
	int keep_reading = 1;
	int wrote;
	void *buffer;
	size_t buffer_len;
	int err;
	int handle;
	PilotRecord remote;
	int entries;
	struct DBInfo dbi;	
	struct pi_file *file;
	int result = 0;
	int reset = 0;
	int version;
	gboolean free_appblock = FALSE;

#ifdef DEBUG
	g_print ("in gnome_real_pilot_conduit_file_install_db\nsrc_file is %s\n", src_file);
#endif

	/* Spam device */
	if (dlp_OpenConduit(psock) < 0) {
#ifdef DEBUG
		fprintf(stderr, "file_conduit: Exiting on cancel, stopped before installing '%s'.\n", src_file);
#endif
		result = -1;
		goto exit;
	}

	version = pi_version (psock);

	/* Open input file */
	file = pi_file_open(src_file);
	if (file == NULL) {
#ifdef DEBUG
		fprintf(stderr,"file_conduit: Unable to open '%s'!\n", src_file);
#endif
		result = -1;
		goto exit;
	}

#ifdef DEBUG
	fprintf(stderr,"file_conduit: Installing %s... ", src_file);
	fflush(stdout);
#endif

	/* Load file info */
	pi_file_get_info (file, &dbi);
	pi_file_get_entries (file, &entries);

	/* Reset if a system patch is installed */
	if (dbi.creator == pi_mktag('p', 't', 'c', 'h')) {
		reset = 1;	
	}
	
	/* Reset if the db requires it */
	if (dbi.flags & dlpDBFlagReset) {
		reset = 1;
	}
	
	/* First delete the db from the device, errors are irrelevant */
	dlp_DeleteDB (psock, cardno, dbi.name);

	/* Judd - 25Nov99 - Graffiti hack 
           We want to make sure that these 2 flags get set for this one */
	/* eskil - 12Sept2001 - I trust Judd */
	if (dbi.creator == pi_mktag('g', 'r', 'a', 'f')) {
                dbi.flags |= dlpDBFlagNewer;
                dbi.flags |= dlpDBFlagReset;
        }
	
	/* Create the DB */
	err = dlp_CreateDB (psock,
			    dbi.creator,
			    dbi.type,
			    cardno,
			    dbi.flags,
			    dbi.version,
			    dbi.name,
			    &handle);
	
	if (err < 0) {
		int retry = 0;

                /* Judd - 25Nov99 - Graffiti hack

                   The dlpDBFlagNewer specifies that if a DB is open and
                   cannot be deleted then it can be overwritten by a DB with
                   a different name.  The creator ID of "graf" is what
                   really identifies a DB, not the name.  We could call it
                   JimBob and the palm would still find it and use it. */

		/* eskil - 12Sept2001 - I trust Judd */

                if (strcmp(dbi.name, "Graffiti ShortCuts ") == 0) {
                        strcpy(dbi.name, "Graffiti ShortCuts");
                        retry = 1;
                } else if (strcmp(dbi.name, "Graffiti ShortCuts") ==
                           0) {
                        strcpy(dbi.name, "Graffiti ShortCuts ");
                        retry = 1;
                } else if (dbi.creator ==
                           pi_mktag('g', 'r', 'a', 'f')) {
                        /* Yep, someone has named it JimBob */
                        strcpy(dbi.name, "Graffiti ShortCuts");
                        retry = 1;
                }

		/* eskil - 12Sept2001 - Seems that Net Prefs is under the same curse... */
                if (strcmp(dbi.name, "Net Prefs ") == 0) {
                        strcpy(dbi.name, "Net Prefs");
                        retry = 1;
                } else if (strcmp(dbi.name, "Net Prefs") ==
                           0) {
                        strcpy(dbi.name, "Net Prefs ");
                        retry = 1;
                } else if (dbi.creator ==
                           pi_mktag('n', 'e', 't', 'l')) {
                        /* Yep, someone has named it JimBob */
                        strcpy(dbi.name, "Net Prefs");
                        retry = 1;
                }		
		
                if (retry) {
                        /* Judd - 25Nov99 - Graffiti hack
                           We changed the name, now we can try to write it
                           again */
			
			if (dlp_CreateDB (psock, dbi.creator, dbi.type,
					  cardno, dbi.flags, dbi.version,
					  dbi.name, &handle) < 0) {
				g_warning ("error (%d) in creating '%s'", err, dbi.name);
				g_warning ("creator = %lu\ntype = %lu\nflags = %u\nversion = %u\n",
					   dbi.creator, dbi.type, 
					   dbi.flags, dbi.version);
				gnome_pilot_conduit_send_error(GNOME_PILOT_CONDUIT(conduit),
							       _("Install of %s failed"),
							       dbi.name);
				goto close_and_exit;
                        }
                } else {	
			g_warning ("error (%d) in creating '%s'", err, dbi.name);
			g_warning ("creator = %lu\ntype = %lu\nflags = %u\nversion = %u\n",
				   dbi.creator, dbi.type, 
				   dbi.flags, dbi.version);
			gnome_pilot_conduit_send_error(GNOME_PILOT_CONDUIT(conduit),
						       _("Install of %s failed"),
						       dbi.name);
			goto close_and_exit;
		}
	}

	/* Get and load the AppInfo block (if any) */
	pi_file_get_app_info (file, &buffer, &buffer_len);	

        /* Compensate for bug in OS 2.x Memo */
        if ((version > 0x0100) && (strcmp(dbi.name, "MemoDB") == 0)
            && (buffer_len > 0) && (buffer_len < 282)) {

                /* Justification: The appInfo structure was accidentally
                   lengthend in OS 2.0, but the Memo application does not
                   check that it is long enough, hence the shorter block
                   from OS 1.x will cause the 2.0 Memo application to lock
                   up if the sort preferences are modified. This code
                   detects the installation of a short app info block on a
                   2.0 machine, and lengthens it. This transformation will
                   never lose information. */

                void *buffer_two = calloc(1, 282);

                memcpy(buffer_two, buffer, buffer_len);
                buffer = buffer_two;
                buffer_len = 282;
                free_appblock = TRUE;
        }	

	if (buffer_len > 0) {
		dlp_WriteAppBlock (psock, handle, buffer, buffer_len);
	}
	if (free_appblock) {
		free (buffer);
	}
		
	g_message(_("Installing %s from %s..."), dbi.name, src_file);
	gnome_pilot_conduit_send_message(GNOME_PILOT_CONDUIT(conduit),
					 _("Installing %s..."),
					 dbi.name);

	
	index = 0;
	wrote = 0;
	do {
		unsigned long type;
		int id;
		
		if (dbi.flags & dlpDBFlagResource) {
			keep_reading = (pi_file_read_resource (file,
							       index, 
							       &buffer,
							       &buffer_len,
							       &type,
							       &id) >= 0);
#ifdef DEBUG_REC_IO
			g_message ("read resource id = %d, type = %s, size %d, index %d/%d",
				   id, pi_unmktag (type), buffer_len,
				   index+1, entries);
#endif

			if (buffer_len == 0) continue;

			if (buffer_len > 65535) {
				result = -1;
			} else {				
				if (type == pi_mktag('b', 'o', 'o', 't')) {
					reset = 1;
				}
								
				if (keep_reading) {				
					err = dlp_WriteResource (psock,
								 handle,
								 type,
								 id,
								 buffer,
								 buffer_len);					
					
					if (err < 0) {
						g_warning ("error in writing to db");
						result = -1;
					} else {
						wrote++;
#ifdef DEBUG_REC_IO
						g_message ("write resource id = %d, type = %s, size %d, index %d/%d",
							   id, pi_unmktag (type), buffer_len,
							   index+1, entries);
#endif
					}

				}
			}
		} else {
			keep_reading = (pi_file_read_record (file,
							     index,
							     &remote.buffer,
							     &remote.len,
							     &remote.flags,
							     &remote.catID,
							     &remote.recID) >= 0);
			
#ifdef DEBUG_REC_IO
			g_message ("read record id = %d (size %d), index %d/%d",
				   remote.recID, remote.len,
				   index+1, entries);
#endif
			
			if (remote.len == 0) continue;

			if (remote.len > 65535) {
				result = -1;
			} else {
				if (keep_reading) {
					err = dlp_WriteRecord (psock,
							       handle,
							       remote.flags,
							       remote.recID,
							       remote.catID,
							       remote.buffer,
							       remote.len,
							       0);
					if (err < 0) {
						g_warning ("error in writing to db");
						result = -1;
					} else {
						wrote++;
#ifdef DEBUG_REC_IO
						g_message ("write record id = %d (size %d), index %d/%d",
							   remote.recID, remote.len,
							   index+1, entries);
#endif
					}
				}
			}
			
		}			
		index++;
		gnome_pilot_conduit_send_progress (GNOME_PILOT_CONDUIT (conduit),
						   entries,
						   index);
	} while (keep_reading && result == 0 && index < entries);				  

	dlp_CloseDB (psock,handle);

	{
		char *log;
		g_message ("Wrote %d of %d %s, which is %s",
			   wrote, entries, dbi.flags & dlpDBFlagResource ? "resources" : "records",
			   wrote == entries ? "good" : "BAD");
			   
		if (result == 0) {			
			log = g_strdup_printf ("Installed %s\n ", dbi.name);
			if (rm) {
				unlink (src_file);
			}
			
			if (reset) {
				g_message ("dlp_ResetSystem (%d)", psock);
				dlp_ResetSystem (psock);
			}
		} else {
			log = g_strdup_printf ("Install of %s failed\n ", dbi.name);
			dlp_DeleteDB (psock, cardno, dbi.name);
		}
		dlp_AddSyncLogEntry (psock, log);
		g_free (log);
	}

 close_and_exit:
	pi_file_close (file);

 exit:
	return result;
};

GnomePilotConduit *
conduit_load_gpilot_conduit (GPilotPilot *pilot)
{
	GnomePilotConduit *retval;

	retval = GNOME_PILOT_CONDUIT (gnome_pilot_conduit_file_new (pilot));
	g_assert (retval != NULL);

	gnome_pilot_conduit_file_connect__install_db (GNOME_PILOT_CONDUIT_FILE(retval),
	    gnome_real_pilot_conduit_file_install_db, NULL);

	return retval;
}

void
conduit_destroy_gpilot_conduit (GnomePilotConduit *conduit)
{
	g_object_unref (G_OBJECT (conduit));
}

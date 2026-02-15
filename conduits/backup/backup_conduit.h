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
 *          Robert Mibus
 */

/* $Id$ */
#ifndef __BACKUP_CONDUIT_H__
#define __BACKUP_CONDUIT_H__

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define OBJ_DATA_CONDUIT "conduit_data"
#define OBJ_DATA_CONFIG  "conduit_config"
#define OBJ_DATA_OLDCONFIG  "conduit_oldconfig"
#define OBJ_DATA_CONFIG_WINDOW  "config_window"

typedef struct ConduitCfg {
  gchar *backup_dir;
  gchar *old_backup_dir; /* restore the original value if users set an invalid backup directory */
  GList *exclude_files;
  GList *files_in_backup;   /* contains the file in backup, any entries when
                               destroy is called are considered deleted on PDA */
  int no_of_backups;        /* # of backups to keep after the main one */
  gboolean remove_deleted;
  gboolean updated_only;
  guint32 pilotId;
  pid_t child;
} ConduitCfg;

#define GET_CONDUIT_CFG(s) ((ConduitCfg*)g_object_get_data(G_OBJECT(s),OBJ_DATA_CONFIG))
#define GET_CONDUIT_OLDCFG(s) ((ConduitCfg*)g_object_get_data(G_OBJECT(s),OBJ_DATA_OLDCONFIG))
#define GET_CONDUIT_DATA(s) ((ConduitData*)g_object_get_data(G_OBJECT(s),OBJ_DATA_CONDUIT))
#define GET_CONDUIT_WINDOW(s) ((GtkWidget*)g_object_get_data(G_OBJECT(s),OBJ_DATA_CONFIG_WINDOW))

#endif

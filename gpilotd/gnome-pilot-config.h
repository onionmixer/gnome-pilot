/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- *//* 
 * Copyright (C) 2009 Free Software Foundation
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
 * Authors: Halton Huo <halton.huo@sun.com>
 *
 */

#ifndef _GPILOT_CONFIG_H_
#define _GPILOT_CONFIG_H_

#include <glib.h>

GKeyFile* get_gpilotd_kfile      (void);
GKeyFile* get_queue_kfile        (void);
GKeyFile* get_backup_kfile       (void);
GKeyFile* get_conduits_kfile     (gint id);
GKeyFile* get_pilot_cache_kfile  (gint id);
gboolean  save_gpilotd_kfile     (GKeyFile *kfile);
gboolean  save_queue_kfile       (GKeyFile *kfile);
gboolean  save_backup_kfile      (GKeyFile *kfile);
gboolean  save_conduits_kfile    (GKeyFile *kfile,
				  gint	    id);
gboolean  save_pilot_cache_kfile (GKeyFile *kfile,
				  gint      id);

#endif /* _GPILOT_CONFIG_H_ */

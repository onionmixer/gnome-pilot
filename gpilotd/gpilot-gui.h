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

#ifndef _GPILOT_GUI_H_
#define _GPILOT_GUI_H_
#include <glib.h>
#include "gnome-pilot-structures.h"

void gpilot_gui_warning_dialog (gchar *mesg, ...);
void gpilot_gui_error_dialog (gchar *mesg, ...);

/* given a pilot or pilot==null,
   if pilot!=NULL, ask if this is the pilot to restore,
   or (or if pilot=NULL) let user choose a pilot or
   create a new pilot and return this */
GPilotPilot* gpilot_gui_restore (GPilotContext *context, GPilotPilot *pilot);

#endif /* _GPILOT_GUI_H_ */

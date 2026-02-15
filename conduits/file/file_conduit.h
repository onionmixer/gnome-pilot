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
 *  Authors: Eskil Heyn Olsen
 *           Vadim Strizhevsky
 */

/* $Id$ */
#ifndef __FILE_CONDUIT_H__
#define __FILE_CONDUIT_H__

typedef struct ConduitCfg {
  guint32 pilotId;
  pid_t child;
} ConduitCfg;

typedef struct db {
        char name[256];
        int flags;
        unsigned long creator;
        unsigned long type;
        int maxblock;
} db;

#define CONDUIT_CFG(s) ((ConduitCfg*)(s))


#endif

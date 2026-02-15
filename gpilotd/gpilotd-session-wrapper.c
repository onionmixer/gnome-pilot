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
 * Authors: Manish Vachharajani
 *
 */

#include <stdio.h>


int main(int argc, char *argv[])
{
  printf("session-wrapper called, but no code"
	 " is implemented, sorry :)\n");
  /* do session stuff */
  /*fork and then exec gpilotd --no-pid.  Remeber to pass HUP signals
    and KILL signals.  Handle case when gpilotd dies in SIGCHLD by
    exiting ourselves with the same exit code.  Make sure gpilotd's
    children dying doesn't signal us.*/
  return 0;
}


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
 *          Dave Camp
 *
 */

#include <config.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gnome-pilot-structures.h"
#include <gnome-pilot-config.h>
#include "gpilot-gui.h"
#include <glib/gi18n.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

/* From pi-csd */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pi-source.h>

#define LOCK_DIR "/var/lock"
#define LOCK_BINARY 0

#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN */

/* What, me worry? */
#ifndef IFF_POINTOPOINT
# ifdef IFF_POINTTOPOINT
#  define IFF_POINTOPOINT IFF_POINTTOPOINT
# endif
#endif
/* end of from pi-csd */

static GList *get_devices (void);
static GList *get_pilots (void);


/* context stuff first */
GPilotContext *gpilot_context_new (void)
{
	GKeyFile *kfile;
	GError   *error = NULL;
	
	GPilotContext *retval;
	
	retval = (GPilotContext *)g_malloc (sizeof (GPilotContext));
	retval->paused = FALSE;
	retval->devices = NULL;
	retval->pilots = NULL;
	retval->user = NULL;
#ifdef WITH_USB_VISOR
	retval->visor_fd = -1;
	retval->visor_io = NULL;
	retval->visor_in_handle = -1;
	retval->visor_err_handle = -1;
#endif

	kfile = get_gpilotd_kfile ();

	retval->sync_PC_Id = g_key_file_get_integer (kfile, "General", "sync_PC_Id", &error);
	if (error) {
		/* get the id.  Does anyone know the valid range for this? */
		srand (time (NULL));
		retval->sync_PC_Id = 1 + ((guint) 1000000.0*rand ());
		g_key_file_set_integer (kfile, "General", "sync_PC_Id",
				     retval->sync_PC_Id);
		g_error_free (error);
		error = NULL;
		
	}

	/* get progress stepping, default is -1, if default is returned,
	   default to one and set it */
	retval->progress_stepping = g_key_file_get_integer (kfile, "General", "progress_stepping", &error);
	if (error) {
		retval->progress_stepping = 1;
		g_key_file_set_integer (kfile, "General", "progress_stepping", retval->progress_stepping);
		g_error_free (error);
		error = NULL;
	}

	save_gpilotd_kfile (kfile);
	g_key_file_free (kfile);

	return retval;
}

/* this will initialize the user context from their config
 * files.  If it has already been initialized, it will reread
 * the files, and free the old data. */
void
gpilot_context_init_user (GPilotContext *context)
{
	GKeyFile *kfile;
	gchar *str;

	if (!context->user) {
		context->user = gpilot_user_new ();
	}

	str=getenv ("USER");
	if (str) {
		g_free (context->user->username);
		context->user->username = g_strdup (str);
	}
 
	context->devices = get_devices ();
	context->pilots = get_pilots ();

	kfile = get_gpilotd_kfile ();
	context->sync_PC_Id = g_key_file_get_integer (kfile, "General", "sync_PC_Id", NULL);

	g_key_file_free (kfile);
}

void
gpilot_context_free (GPilotContext *context)
{
	g_free (context->user->username);
	context->user->username = NULL;

	g_free (context->user);
	context->user = NULL;

	g_list_foreach (context->pilots, (GFunc)gpilot_pilot_free, NULL);
	g_list_free (context->pilots);
	context->pilots = NULL;
	
	g_list_foreach (context->devices, (GFunc)gpilot_device_free, NULL);
	g_list_free (context->devices);
	context->devices = NULL;
}

#define LOCK_DIR "/var/lock"

static int
gpilot_hdb_uucp_lock (GPilotDevice *device) 
{
#ifndef LOCK_BINARY
	char lock_buffer[12];
#endif
	int fd, pid, n;
	char *p = NULL;
	char *dev = device->port;

	if (geteuid () != 0) {
		/* g_message ("Not root, won't do UUCP lock on %s", device->port); */
		return 1;
	}
	
	if ((p = strrchr(dev, '/')) != NULL) {
		dev = p + 1;
	}
	
	device->lock_file = g_new0 (gchar, 128);

	g_snprintf (device->lock_file, 127, "%s/LCK..%s", LOCK_DIR, dev);

	while ((fd = open (device->lock_file, O_EXCL | O_CREAT | O_RDWR, 0644)) < 0) {
		if (errno != EEXIST) {
			g_warning ("Can't create lock file %s: %m", device->lock_file);
			break;
		}
		
		/* Read the lock file to find out who has the device locked. */
		fd = open (device->lock_file, O_RDONLY, 0);
		if (fd < 0) {
			if (errno == ENOENT) /* This is just a timing problem. */
				continue;
			g_warning ("Can't open existing lock file %s: %m", device->lock_file);
			break;
		}

#ifndef LOCK_BINARY
		n = read (fd, lock_buffer, 11);
#else
		n = read (fd, &pid, sizeof(pid));
#endif /* LOCK_BINARY */
		close (fd);
		fd = -1;
		if (n <= 0) {
			g_warning ("Can't read pid from lock file %s", device->lock_file);
			break;
		}
		
		/* See if the process still exists. */
#ifndef LOCK_BINARY
		lock_buffer[n] = 0;
		pid = atoi (lock_buffer);
#endif /* LOCK_BINARY */
		if (pid == getpid ()) {
			g_warning ("Port %s is already locked", device->port);
			return 0;           /* somebody else locked it for us */
		}
		if (pid == 0
		    || (kill (pid, 0) == -1 && errno == ESRCH)) {
			if (unlink (device->lock_file) == 0) {
				g_message ("Removed stale lock on %s (pid %d)", device->port, pid);
				continue;
			}
			g_warning ("Couldn't remove stale lock on %s", device->port);
		} else {
			g_message ("Device %s is locked by pid %d", device->port, pid);
		}
		break;
	}

	if (fd < 0) {
		g_free (device->lock_file);
		device->lock_file = NULL;
		return 0;
	}
	
	pid = getpid ();
#ifndef LOCK_BINARY
	snprintf(lock_buffer, 11, "%10d\n", pid);
	if (write (fd, lock_buffer, 11) != 11)
		g_warning("Failed to write to lock file");
#else
	if (write (fd, &pid, sizeof (pid)) != sizeof(pid))
		g_warning("Failed to write to lock file");
#endif
	close (fd);
	return 1;
}


static void
gpilot_hdb_uucp_unlock (GPilotDevice *device) 
{
	if (geteuid () != 0) {
		/* g_message ("Not root, won't do UUCP unlock on %s", device->port); */
	}

	if (device->lock_file) {
		unlink(device->lock_file);	
		g_free (device->lock_file);
		device->lock_file = NULL;
	}
}

/* device stuff next */
GPilotDevice *
gpilot_device_new (void)
{
	GPilotDevice *device;
	device = g_new0(GPilotDevice, 1);
	return device;
}

static gint 
gpilot_serial_device_init (GPilotDevice *device)
{
	if (!gpilot_hdb_uucp_lock (device)) {
		return -1;
	}

	device->fd=open (device->port, O_RDWR|O_NOCTTY|O_NONBLOCK);
	if (device->fd < 0) {
		g_warning (_("Could not open device %s (%s): reason: \"%s\"."),
			  device->name, device->port,
			  g_strerror (errno));
#ifdef GUI
		gpilot_gui_warning_dialog (_("GnomePilot could not open device %s (%s).\n"
					    "Reason: \"%s\"."),
					  device->name, device->port,
					  g_strerror (errno));
#endif
		gpilot_hdb_uucp_unlock (device);
	
		g_free (device->name);
		device->name = NULL;

		g_free (device->port);
		device->port = NULL;

		pi_close (device->fd);
		device->fd = 0;

		device->io = NULL;

		return -1;
	}
	device->io = g_io_channel_unix_new (device->fd);
	g_io_channel_ref (device->io);

	return 0;
}

/* This free is used by USB and IrDA devices as well */
static void
gpilot_serial_device_free (GPilotDevice *device)
{
	if (device->fd) {
		pi_close (device->fd);
	}
	g_free (device->name);
	g_free (device->port);
	gpilot_hdb_uucp_unlock (device);
}

static void
gpilot_serial_device_deinit (GPilotDevice *device)
{
	GError *myerr = NULL;

	if (device->io) {
		g_source_remove (device->in_handle);
		g_source_remove (device->err_handle);
		g_io_channel_shutdown (device->io, 0, &myerr);
		g_io_channel_unref (device->io);
	}
}

#ifdef WITH_USB_VISOR
static gint 
gpilot_usb_device_init (GPilotDevice *device)
{
	gpilot_hdb_uucp_lock (device);
	device->fd = -1;
	device->io = NULL;
	device->device_exists = FALSE;
	return 0;
}

static void
gpilot_usb_device_free (GPilotDevice *device)
{
	gpilot_hdb_uucp_unlock (device);
}
static void
gpilot_usb_device_deinit (GPilotDevice *device)
{
}
#endif /* WITH_USB_VISOR */

#ifdef WITH_IRDA /* WITH_IRDA */
static gint 
gpilot_irda_device_init (GPilotDevice *device)
{
	return gpilot_serial_device_init (device);
}

static void
gpilot_irda_device_free (GPilotDevice *device)
{
	gpilot_serial_device_free (device);
}

static void
gpilot_irda_device_deinit (GPilotDevice *device)
{
	gpilot_serial_device_deinit (device);
}
#endif /* WITH_IRDA */

#ifdef WITH_NETWORK

/* gpilot_network_device_init (GPilotDevice *device)
 * pi-csd.c: Connection Service Daemon, required for accepting 
 *           logons via NetSync (tm)
 * Copyright (c) 1997, Kenneth Albanowski
 *
 */
static gint 
gpilot_network_device_init (GPilotDevice *device)
{
#define MAXLINELEN 256
	char line[MAXLINELEN];
	char pi_net[100];
	int ret, fd;
	static gboolean bluetooth_warning_done = FALSE;

	memset(pi_net, 0, sizeof(pi_net));

	if (device->type == PILOT_DEVICE_BLUETOOTH) {
		strncpy(pi_net, "bt:", 3);
	} else {
		g_assert (device->type == PILOT_DEVICE_NETWORK);
		strncpy(pi_net, "net:", 4);
		if (device->ip != NULL && (*device->ip != 0x0)) {
			struct 	sockaddr_in serv_addr;
			/* Verify the IP address is valid */
			memset(&serv_addr, 0, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_addr.s_addr = inet_addr(device->ip);
			if (serv_addr.sin_addr.s_addr == (in_addr_t)-1) {
				struct hostent *hostent = gethostbyname(device->ip);
				if (!hostent) {
					g_warning ("Device [%s]: Bad IP address/hostname: %s",
					    device->name, device->ip);
					return -1;
				}
			}
			strncat(pi_net, device->ip, sizeof(pi_net) - 2 - strlen(pi_net));
		} else {
			strncat(pi_net, "any", 3);
		}
	}
	device->fd = pi_socket (PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP);
	if (device->fd < 0) {
		g_warning ("Device [%s, %s]: Unable to get socket: %s", 
		    device->name, pi_net, strerror(errno));
		return -1;
	}

	ret = pi_bind (device->fd, pi_net);
	if (ret < 0) {
		if (device->type == PILOT_DEVICE_BLUETOOTH) {
			g_snprintf(line, MAXLINELEN,_("Bluetooth Device [%s]: Unable to "
				"bind socket: err %d (check pilot-link was compiled "
				"with bluetooth support)"),
			    device->name, ret);
			/* only give GUI warning once... */
			if (!bluetooth_warning_done) {
				gpilot_gui_warning_dialog(line);
				bluetooth_warning_done = TRUE;
			}
		} else {
			g_snprintf(line, MAXLINELEN,_("Device [%s, %s]: Unable to "
				"bind socket: err %d"),
			    device->name, pi_net, ret);
		}
		g_warning("%s", line);
		return -1;
	}

	/* Now listen for incoming connections */
	if (pi_listen (device->fd, 1) != 0) {
		g_warning ("Device [%s, %s]: Error from listen: %s",
		    device->name, pi_net, strerror (errno));
		pi_close(device->fd);
		return -1;
	}
	/* Register an interest in the socket, to get events when devices connect */
	fd = dup(device->fd);
	fcntl(fd, F_SETFD, FD_CLOEXEC); /* don't let bonobo keep references alive */
	fcntl(device->fd, F_SETFD, FD_CLOEXEC);
	device->io = g_io_channel_unix_new (fd);
	g_io_channel_ref (device->io);

	/* All went well.. */
	return 0;
}

static void
gpilot_network_device_free (GPilotDevice *device)
{
	if (device->fd) {
		pi_close (device->fd);
	}
	if (device->ip != NULL)
		g_free(device->ip);
}

static void
gpilot_network_device_deinit (GPilotDevice *device)
{
	GError *err = NULL;

	if (device->io) {
		g_source_remove (device->in_handle);
		g_source_remove (device->err_handle);
		g_io_channel_shutdown (device->io, 0, &err);
		if (err != NULL)
		{
			/* Report error to user, and free error */
			g_warning ("error from shutdown: %s\n", err->message);
			g_error_free (err);
		} 
		g_io_channel_unref (device->io);
	}
}
#endif /* WITH_NETWORK */

gint
gpilot_device_init (GPilotDevice *device)
{
	gint result;

	g_return_val_if_fail (device != NULL,-1);
	
	if (device->type == PILOT_DEVICE_SERIAL) {
		result = gpilot_serial_device_init (device);
#ifdef WITH_IRDA
	} else if (device->type == PILOT_DEVICE_IRDA) {
		result = gpilot_irda_device_init (device);
#endif /* WITH_IRDA */
#ifdef WITH_USB_VISOR
	} else if (device->type == PILOT_DEVICE_USB_VISOR) {
		result = gpilot_usb_device_init (device);
#endif /* WITH_USB_VISOR */
#ifdef WITH_NETWORK
	} else if (device->type == PILOT_DEVICE_NETWORK) {
		result = gpilot_network_device_init (device);
	} else if (device->type == PILOT_DEVICE_BLUETOOTH) {
		result = gpilot_network_device_init (device);
#endif /* WITH_NETWORK */
	} else {
		g_warning (_("Unknown device type"));
		result = -1;
	}

	return result;
}

gint
gpilot_device_load (GPilotDevice *device, gint i)
{
	gchar *iDevice;
	gint result = 0;
	GKeyFile *kfile;
	GError   *error = NULL;

	g_return_val_if_fail (device != NULL,-1);
	
	kfile = get_gpilotd_kfile ();
	iDevice = g_strdup_printf ("Device%d", i);

	device->type = g_key_file_get_integer (kfile, iDevice, "type", &error);
	if (error) {
		g_error_free (error);
		error = NULL;
		device->type = 0;
	}
	device->name = g_key_file_get_string (kfile, iDevice, "name", NULL);
	device->timeout = g_key_file_get_integer (kfile, iDevice, "timeout", &error);
	if (error) {
		g_error_free (error);
		error = NULL;
		device->timeout = 3;
	}

	switch (device->type) {
	case PILOT_DEVICE_SERIAL:
	case PILOT_DEVICE_USB_VISOR:
	case PILOT_DEVICE_IRDA:
		/* These devices share the serial loader */
		device->port = g_key_file_get_string (kfile, iDevice, "device", NULL);
		device->speed = (guint)g_key_file_get_integer (kfile, iDevice, "speed", &error);
		if (error) {
			g_error_free (error);
			error = NULL;
			device->speed = 57600;
		}
		break;
	case PILOT_DEVICE_NETWORK:
		device->ip = g_key_file_get_string (kfile, iDevice, "ip", NULL);
		break;
	case PILOT_DEVICE_BLUETOOTH:
		break; /* no further info required */
	default:
		g_warning (_("Unknown device type"));
	}

	g_free (iDevice);
	g_key_file_free (kfile);

	return result;
}

void
gpilot_device_deinit (GPilotDevice *device)
{
	g_assert (device != NULL);

	if (device->type == PILOT_DEVICE_SERIAL) {
		gpilot_serial_device_deinit (device);
#ifdef WITH_IRDA
	} else if (device->type == PILOT_DEVICE_IRDA) {
		gpilot_irda_device_deinit (device);
#endif /* WITH_IRDA */
#ifdef WITH_USB_VISOR
	} else if (device->type == PILOT_DEVICE_USB_VISOR) {
		gpilot_usb_device_deinit (device);
#endif /* WITH_USB_VISOR */
#ifdef WITH_NETWORK
	} else if (device->type == PILOT_DEVICE_NETWORK) {
		gpilot_network_device_deinit (device);
	} else if (device->type == PILOT_DEVICE_BLUETOOTH) {
		gpilot_network_device_deinit (device);
#endif /* WITH_NETWORK */
	} else {
		g_warning (_("Unknown device type"));
	}
}

void
gpilot_device_free (GPilotDevice *device)
{
	g_assert (device != NULL);

	gpilot_device_deinit (device);

	if (device->type == PILOT_DEVICE_SERIAL) {
		gpilot_serial_device_free (device);
#ifdef WITH_IRDA
	} else if (device->type == PILOT_DEVICE_IRDA) {
		gpilot_irda_device_free (device);
#endif /* WITH_IRDA */
#ifdef WITH_USB_VISOR
	} else if (device->type == PILOT_DEVICE_USB_VISOR) {
		gpilot_usb_device_free (device);
#endif /* WITH_USB_VISOR */
#ifdef WITH_NETWORK
	} else if (device->type == PILOT_DEVICE_NETWORK) {
		gpilot_network_device_free (device);
	} else if (device->type == PILOT_DEVICE_BLUETOOTH) {
		gpilot_network_device_free (device);
#endif /* WITH_NETWORK */
	} else {
		g_warning (_("Unknown device type"));
	}

	g_free (device);
}

static GList *
get_devices (void)
{
	GList * retval = NULL;
	int n, i, final;
	GKeyFile *kfile;

	kfile = get_gpilotd_kfile ();

	final = n = g_key_file_get_integer (kfile, "General", "num_devices", NULL);

	if (n==0) {
		g_warning (_("Number of devices is configured to 0"));
#ifdef GUI
		gpilot_gui_warning_dialog (_("No devices configured.\n"
					    "Please run gpilotd-control-applet (use gnomecc)\n" 
					    "to configure gnome-pilot."));    
#endif
	}

	for (i=0;i<n;i++) {
		GPilotDevice *device;

		device = gpilot_device_new ();

		if (gpilot_device_load (device, i) == 0) {
			if (gpilot_device_init (device) == 0) {
				retval = g_list_append (retval, device);
			}
		} else {
			final --;
		}
	}

	if (final == 0) {
		g_warning (_("No accessible devices available"));
	}

	g_key_file_free (kfile);
	return retval;
}

static GList *
get_pilots (void)
{
	GList * retval = NULL;
	int n, i;
	GKeyFile *kfile;

	kfile = get_gpilotd_kfile ();
  
	n = g_key_file_get_integer (kfile, "General", "num_pilots", NULL);

	if (n==0) {
		g_warning (_("Number of PDAs is configured to 0"));
#ifdef GUI
		gpilot_gui_warning_dialog (_("No PDAs configured.\n"
					    "Please run gpilotd-control-applet (use gnomecc)\n" 
					    "to configure gnome-pilot."));    
#endif
	}


	for (i=0; i<n; i++) {
		GPilotPilot *pilot;
		pilot = gpilot_pilot_new ();
		gpilot_pilot_init (pilot, i);
		retval = g_list_append (retval, pilot);
	}

	g_key_file_free (kfile);
	return retval;
}

GPilotPilot *
gpilot_pilot_new (void)
{
	GPilotPilot *retval;
	retval = g_new0(GPilotPilot,1);
	return retval;
}

void
gpilot_pilot_init (GPilotPilot *pilot, 
		   gint i)
{
	gchar *iPilot;
	GKeyFile *kfile;

	/* set up stuff  */
	g_free (pilot->name);
	g_free (pilot->passwd);
	g_free (pilot->pilot_username);
	g_free (pilot->pilot_charset);

	kfile = get_gpilotd_kfile ();
	iPilot = g_strdup_printf ("Pilot%d", i);

	/* start filling in fields */
	pilot->name = g_key_file_get_string (kfile, iPilot, "name", NULL);
	pilot->pilot_id = g_key_file_get_integer (kfile, iPilot, "pilotid", NULL);
	pilot->pilot_username = g_key_file_get_string (kfile, iPilot, "pilotusername", NULL);
	pilot->passwd = g_key_file_get_string (kfile, iPilot, "password", NULL);
	pilot->creation = g_key_file_get_integer (kfile, iPilot, "creation", NULL);
	pilot->romversion = g_key_file_get_integer (kfile, iPilot, "romversion", NULL);
	pilot->number=i;
	pilot->sync_options.basedir = g_key_file_get_string (kfile, iPilot, "basedir", NULL);
	pilot->pilot_charset = g_key_file_get_string (kfile, iPilot, "charset", NULL);
	/* If no charset has been specified by user, fall back
	 * to the PILOT_CHARSET environment variable, if that
	 * has been specified.
	 */
	if (pilot->pilot_charset == NULL) {
		if ((pilot->pilot_charset
			= getenv("PILOT_CHARSET")) != NULL) {
			pilot->pilot_charset =
			    g_strdup (pilot->pilot_charset);
		} else {
			g_warning (_("No pilot_charset specified.  Using `%s'."),
			    GPILOT_DEFAULT_CHARSET);
			pilot->pilot_charset =
			    g_strdup (GPILOT_DEFAULT_CHARSET);
		}
	}

	g_key_file_free (kfile);
	g_free (iPilot);
}

void
gpilot_pilot_free (GPilotPilot *pilot)
{
	g_free (pilot->name);
	g_free (pilot->passwd);
	g_free (pilot->pilot_username);
	g_free (pilot->sync_options.basedir);
	g_free (pilot->pilot_charset);
	g_free (pilot);
}

GPilotUser *
gpilot_user_new (void) {
	GPilotUser *retval;

	retval = (GPilotUser *)g_malloc(sizeof(GPilotUser));
	retval->username = NULL;

	return retval;
}

void
gpilot_user_free (GPilotUser *user)
{
	g_free (user->username);
	g_free (user);
}
 
GPilotPilot*
gpilot_find_pilot_by_name(gchar *id,GList *inlist) 
{
	GList *iterator;
	iterator = inlist;
	while(iterator!=NULL) {
		GPilotPilot *retval;
		retval = GPILOT_PILOT(iterator->data);
		if (strcmp(retval->name,id)==0) return retval;
		iterator = iterator->next;
	}
	return NULL;
}

GPilotPilot*
gpilot_find_pilot_by_id(guint32 id,GList *inlist) 
{
	GList *iterator;
	iterator = inlist;
	while(iterator!=NULL) {
		GPilotPilot *retval;
		retval = GPILOT_PILOT(iterator->data);
		if (retval->pilot_id ==id) return retval;
		iterator = iterator->next;
	}
	return NULL;
}

gint
gnome_pilot_conduit_sync_type_str_to_int(const gchar *s) 
{
	g_return_val_if_fail(s!=NULL,GnomePilotConduitSyncTypeNotSet);

	if (strcmp (s, GnomePilotConduitSyncTypeSynchronizeStr) == 0) {
		return GnomePilotConduitSyncTypeSynchronize;
	} else if (strcmp (s, GnomePilotConduitSyncTypeCopyToPilotStr) == 0) {
		return GnomePilotConduitSyncTypeCopyToPilot;
	} else if (strcmp (s, GnomePilotConduitSyncTypeCopyFromPilotStr) == 0) {
		return GnomePilotConduitSyncTypeCopyFromPilot;
	} else if (strcmp (s, GnomePilotConduitSyncTypeMergeToPilotStr) == 0) {
		return GnomePilotConduitSyncTypeMergeToPilot;
	} else if (strcmp (s, GnomePilotConduitSyncTypeMergeFromPilotStr) == 0) {
		return GnomePilotConduitSyncTypeMergeFromPilot;
	} else if (strcmp (s, GnomePilotConduitSyncTypeCustomStr) == 0) {
		return GnomePilotConduitSyncTypeCustom;
	} else if (strcmp (s, GnomePilotConduitSyncTypeNotSetStr) == 0) {
		return GnomePilotConduitSyncTypeNotSet;
	}
	return GnomePilotConduitSyncTypeNotSet;
}

const gchar* 
gnome_pilot_conduit_sync_type_int_to_str(GnomePilotConduitSyncType e) 
{
	switch(e) {
	case GnomePilotConduitSyncTypeCustom:
		return GnomePilotConduitSyncTypeCustomStr;
	case GnomePilotConduitSyncTypeSynchronize:
		return GnomePilotConduitSyncTypeSynchronizeStr;
	case GnomePilotConduitSyncTypeCopyFromPilot:
		return GnomePilotConduitSyncTypeCopyFromPilotStr;
	case GnomePilotConduitSyncTypeCopyToPilot:
		return GnomePilotConduitSyncTypeCopyToPilotStr;
	case GnomePilotConduitSyncTypeMergeFromPilot:
		return GnomePilotConduitSyncTypeMergeFromPilotStr;
	case GnomePilotConduitSyncTypeMergeToPilot:
		return GnomePilotConduitSyncTypeMergeToPilotStr;
	case GnomePilotConduitSyncTypeNotSet:
		return GnomePilotConduitSyncTypeNotSetStr;
	default: 
	  g_message ("gnome_pilot_conduit_sync_type_int_to_str: invalid sync_type %d",e);
	  return GnomePilotConduitSyncTypeNotSetStr;
	}
}

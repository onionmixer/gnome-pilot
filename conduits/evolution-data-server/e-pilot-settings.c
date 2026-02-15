/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		JP Rosevear <jpr@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <libedataserver/libedataserver.h>
#include "e-pilot-settings.h"

struct _EPilotSettingsPrivate
{
	ESourceRegistry *registry;
	GtkWidget *source;
	GtkWidget *secret;
	GtkWidget *cat;
	GtkWidget *cat_btn;
};

static void class_init (EPilotSettingsClass *klass);
static void init (EPilotSettings *ps);

static GObjectClass *parent_class = NULL;

GType
e_pilot_settings_get_type (void)
{
	GType type = g_type_from_name ((const gchar *) "EPilotSettings");

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EPilotSettingsClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) class_init,
                        NULL, NULL,
                        sizeof (EPilotSettings),
                        0,
                        (GInstanceInitFunc) init
                };
		type = g_type_register_static (GTK_TYPE_GRID, "EPilotSettings", &info, 0);
	}

	return type;
}

static void
class_init (EPilotSettingsClass *klass)
{
	parent_class = g_type_class_ref (GTK_TYPE_GRID);
}

static void
init (EPilotSettings *ps)
{
	EPilotSettingsPrivate *priv;

	priv = g_new0 (EPilotSettingsPrivate, 1);

	ps->priv = priv;
}

static void
build_ui (EPilotSettings *ps, ESourceRegistry *registry, const gchar *extension_name)
{
	EPilotSettingsPrivate *priv;
	GtkWidget *lbl;
	GList *sources, *l;

	priv = ps->priv;

	priv->registry = g_object_ref (registry);

	/* GtkGrid auto-sizes, no need for gtk_table_resize */
	gtk_widget_set_margin_start (GTK_WIDGET (ps), 4);
	gtk_widget_set_margin_end (GTK_WIDGET (ps), 4);
	gtk_widget_set_margin_top (GTK_WIDGET (ps), 4);
	gtk_widget_set_margin_bottom (GTK_WIDGET (ps), 4);
	gtk_grid_set_column_spacing (GTK_GRID (ps), 6);

	lbl = gtk_label_new (_("Sync with:"));
	gtk_widget_set_halign (lbl, GTK_ALIGN_START);
	gtk_widget_set_valign (lbl, GTK_ALIGN_CENTER);

	/* Replace e_source_combo_box_new with GtkComboBoxText */
	priv->source = gtk_combo_box_text_new ();
	sources = e_source_registry_list_sources (registry, extension_name);
	for (l = sources; l != NULL; l = l->next) {
		ESource *source = E_SOURCE (l->data);
		if (e_source_get_enabled (source)) {
			gtk_combo_box_text_append (
				GTK_COMBO_BOX_TEXT (priv->source),
				e_source_get_uid (source),
				e_source_get_display_name (source));
		}
	}
	g_list_free_full (sources, g_object_unref);

	/* Select the first entry by default */
	gtk_combo_box_set_active (GTK_COMBO_BOX (priv->source), 0);

	gtk_grid_attach (GTK_GRID (ps), lbl, 0, 0, 1, 1);
	gtk_grid_attach (GTK_GRID (ps), priv->source, 1, 0, 1, 1);

	lbl = gtk_label_new (_("Sync Private Records:"));
	gtk_widget_set_halign (lbl, GTK_ALIGN_START);
	gtk_widget_set_valign (lbl, GTK_ALIGN_CENTER);
	priv->secret = gtk_check_button_new ();
	gtk_grid_attach (GTK_GRID (ps), lbl, 0, 1, 1, 1);
	gtk_grid_attach (GTK_GRID (ps), priv->secret, 1, 1, 1, 1);

#if 0
	lbl = gtk_label_new (_("Sync Categories:"));
	gtk_widget_set_halign (lbl, GTK_ALIGN_START);
	gtk_widget_set_valign (lbl, GTK_ALIGN_CENTER);
	priv->cat = gtk_check_button_new ();
	gtk_grid_attach (GTK_GRID (ps), lbl, 0, 2, 1, 1);
	gtk_grid_attach (GTK_GRID (ps), priv->cat, 1, 2, 1, 1);
#endif
}



GtkWidget *
e_pilot_settings_new (ESourceRegistry *registry, const gchar *extension_name)
{
	EPilotSettings *ps;

	ps = g_object_new (E_TYPE_PILOT_SETTINGS, NULL);

	build_ui (ps, registry, extension_name);

	return GTK_WIDGET (ps);
}

ESource *
e_pilot_settings_get_source (EPilotSettings *ps)
{
	EPilotSettingsPrivate *priv;
	const gchar *uid;

	g_return_val_if_fail (ps != NULL, NULL);
	g_return_val_if_fail (E_IS_PILOT_SETTINGS (ps), NULL);

	priv = ps->priv;
	uid = gtk_combo_box_get_active_id (GTK_COMBO_BOX (priv->source));
	if (uid == NULL)
		return NULL;

	return priv->registry ? e_source_registry_ref_source (priv->registry, uid) : NULL;
}

void
e_pilot_settings_set_source (EPilotSettings *ps, ESource *source)
{
	EPilotSettingsPrivate *priv;

	g_return_if_fail (ps != NULL);
	g_return_if_fail (E_IS_PILOT_SETTINGS (ps));

	priv = ps->priv;

	if (source) {
		gtk_combo_box_set_active_id (
			GTK_COMBO_BOX (priv->source),
			e_source_get_uid (source));
	}
}

gboolean
e_pilot_settings_get_secret (EPilotSettings *ps)
{
	EPilotSettingsPrivate *priv;

	g_return_val_if_fail (ps != NULL, FALSE);
	g_return_val_if_fail (E_IS_PILOT_SETTINGS (ps), FALSE);

	priv = ps->priv;

	return gtk_check_button_get_active (GTK_CHECK_BUTTON (priv->secret));
}

void
e_pilot_settings_set_secret (EPilotSettings *ps, gboolean secret)
{
	EPilotSettingsPrivate *priv;

	g_return_if_fail (ps != NULL);
	g_return_if_fail (E_IS_PILOT_SETTINGS (ps));

	priv = ps->priv;

	gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->secret),
				     secret);
}


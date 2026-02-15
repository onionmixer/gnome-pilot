/*
 * gpilot-tray-sni.h — StatusNotifierItem D-Bus interface definitions
 *
 * Implements org.kde.StatusNotifierItem protocol via GDBus.
 * Reference: https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/
 *
 * Copyright (C) 2024 Free Software Foundation
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __GPILOT_TRAY_SNI_H__
#define __GPILOT_TRAY_SNI_H__

#include <gio/gio.h>

/* D-Bus well-known names and paths */
#define SNI_OBJECT_PATH       "/StatusNotifierItem"
#define SNI_MENU_OBJECT_PATH  "/MenuBar"
#define SNI_WATCHER_BUS_NAME  "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_OBJ_PATH  "/StatusNotifierWatcher"
#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"
#define SNI_INTERFACE         "org.kde.StatusNotifierItem"

/* Introspection XML for org.kde.StatusNotifierItem */
static const gchar sni_introspection_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    ""
    "    <!-- Properties -->"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='u' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionMovieName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    ""
    "    <!-- Methods -->"
    "    <method name='ContextMenu'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg name='delta' type='i' direction='in'/>"
    "      <arg name='orientation' type='s' direction='in'/>"
    "    </method>"
    ""
    "    <!-- Signals -->"
    "    <signal name='NewTitle'/>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewAttentionIcon'/>"
    "    <signal name='NewOverlayIcon'/>"
    "    <signal name='NewToolTip'/>"
    "    <signal name='NewStatus'>"
    "      <arg name='status' type='s'/>"
    "    </signal>"
    ""
    "  </interface>"
    "</node>";

#endif /* __GPILOT_TRAY_SNI_H__ */

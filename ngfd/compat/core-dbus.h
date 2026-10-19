/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2017 Jolla Ltd
 * Contact: Juho Hämäläinen <juho.hamalainen@jolla.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef N_DBUS_HELPER_H
#define N_DBUS_HELPER_H

#include <ngf/core.h>
#include <dbus/dbus.h>

typedef DBusHandlerResult (*NDBusFilterFunc) (NCore *core,
                                              DBusConnection *connection,
                                              DBusMessage *msg,
                                              void *userdata);

typedef void (*NDBusReplyFunc) (NCore *core,
                                DBusMessage *msg,
                                void *userdata);

/** Start listening for DBus signal
 * @param core NCore structure
 * @param cb Callback called when signal witht the specified parameters is seen
 * @param userdata Data passed to callback
 * @param type Whether to use system or session bus
 * @param iface DBus interface
 * @param path DBus path
 * @param member DBus signal member
 * @return Zero on failure, otherwise identifier for match which can be used
 *         with n_dbus_remove_match_by_id()
 */
guint       n_dbus_add_match (NCore            *core,
                              NDBusFilterFunc   cb,
                              void             *userdata,
                              DBusBusType       type,
                              const char       *iface,
                              const char       *path,
                              const char       *member);

/** Remove DBus signal listening
 * @param core NCore structure
 * @param match_id Id for the match to remove
 */
void        n_dbus_remove_match_by_id (NCore *core, guint match_id);

/** Remove DBus signal listening
 * @param core NCore structure
 * @param cb Callback function used to add the match
 */
void        n_dbus_remove_match_by_cb (NCore *core, NDBusFilterFunc cb);

/** Make an asynchronous DBus call. If callback is defined pending call
 * is set up and the callback is called with the pending call reply
 * contents.
 * Message part of pending call reply is passed to the NDBusFilterFunc callback.
 * @param core NCore structure
 * @param cb Callback called when pending call reply is received
 * @param userdata Data passed to callback
 * @param type Whether to use system or session bus
 * @param destination DBus destination
 * @param iface DBus interface
 * @param path DBus path
 * @param method DBus method
 * @return TRUE if sending the message succeeded.
 */
gboolean    n_dbus_async_call (NCore           *core,
                               NDBusReplyFunc   cb,
                               void            *userdata,
                               DBusBusType      type,
                               const char      *destination,
                               const char      *path,
                               const char      *iface,
                               const char      *method);

/** Make an asynchronous DBus call. If callback is defined pending call
 * is set up and the callback is called with the pending call reply
 * contents.
 * Message part of pending call reply is passed to the NDBusFilterFunc callback.
 * @param core NCore structure
 * @param cb Callback called when pending call reply is received
 * @param userdata Data passed to callback
 * @param type Whether to use system or session bus
 * @param msg DBus message to send
 * @return TRUE if sending the message succeeded.
 */
gboolean    n_dbus_async_call_full (NCore          *core,
                                    NDBusReplyFunc  cb,
                                    void           *userdata,
                                    DBusBusType     type,
                                    DBusMessage    *msg);

#endif

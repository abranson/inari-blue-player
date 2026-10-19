/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2026 Jolla Mobile Ltd
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <string.h>
#include <dbus/dbus.h>
#include <mce/dbus-names.h>
#include "compat/core-dbus.h"
#include <ngf/event.h>
#include <ngf/plugin.h>
#include <ngf/sinkinterface.h>

#define DATA_KEY "plugin.inari-blue.data"
#define SERVICE "org.sailfishos.InariBlue1"
#define PATH "/org/sailfishos/InariBlue1"
#define TRANSIENT_MS 6000

N_PLUGIN_NAME("inari-blue")
N_PLUGIN_VERSION("0.1")
N_PLUGIN_DESCRIPTION("Inari Blue category feedback")

typedef struct {
    NRequest *request;
    NSinkInterface *iface;
    guint cookie;
    const char *category;
    guint timer;
    guint references;
    gboolean stopped;
    gboolean start_sent;
    gboolean stop_sent;
    gboolean repeating;
    gboolean paused;
    gchar *mce_pattern;
    gboolean completed;
    guint generation;
} InariEvent;

typedef struct {
    InariEvent *event;
    guint generation;
} StartReply;

static GList *active;

static const char *category_for(const char *name)
{
    if (!g_strcmp0(name, "ringtone")) return "ringtone";
    if (!g_strcmp0(name, "voip_ringtone")) return "internet-call";
    if (!g_strcmp0(name, "sms") || !g_strcmp0(name, "sms_fg") || !g_strcmp0(name, "sms_exists")) return "message";
    if (!g_strcmp0(name, "chat") || !g_strcmp0(name, "chat_fg") || !g_strcmp0(name, "chat_exists") || !g_strcmp0(name, "social")) return "chat";
    if (!g_strcmp0(name, "email") || !g_strcmp0(name, "email_exists")) return "email";
    if (!g_strcmp0(name, "clock")) return "clock";
    if (!g_strcmp0(name, "calendar")) return "calendar";
    if (!g_strcmp0(name, "notification") || !g_strcmp0(name, "general_warning")) return "other";
    return NULL;
}
static void unref_event(InariEvent *event)
{
    if (!--event->references) {
        g_free(event->mce_pattern);
        g_free(event);
    }
}
static DBusMessage *event_message(InariEvent *event, gboolean start)
{
    DBusMessage *message = dbus_message_new_method_call(SERVICE, PATH, SERVICE, start ? "StartEvent" : "StopEvent");
    dbus_uint32_t cookie = event->cookie;
    if (!message) return NULL;
    /* Never wake an unavailable player later, after the original alert ended. */
    dbus_message_set_auto_start(message, FALSE);
    if (!dbus_message_append_args(message, DBUS_TYPE_UINT32, &cookie, DBUS_TYPE_INVALID)
        || (start && !dbus_message_append_args(message, DBUS_TYPE_STRING, &event->category, DBUS_TYPE_INVALID))) {
        dbus_message_unref(message);
        return NULL;
    }
    return message;
}
static void stop_event(InariEvent *event)
{
    if (event->start_sent && !event->stop_sent) {
        DBusMessage *message = event_message(event, FALSE);
        if (message) {
            n_dbus_async_call_full(n_sink_interface_get_core(event->iface), NULL, NULL, DBUS_BUS_SESSION, message);
            dbus_message_unref(message);
        }
        event->stop_sent = TRUE;
    }
}
static gboolean complete_event(gpointer data)
{
    InariEvent *event = data;
    event->timer = 0;
    stop_event(event);
    if (!event->stopped && !event->completed) {
        event->completed = TRUE;
        n_sink_interface_complete(event->iface, event->request);
    }
    return G_SOURCE_REMOVE;
}
static void start_reply(NCore *core, DBusMessage *message, void *data)
{
    StartReply *reply = data;
    InariEvent *event = reply->event;
    dbus_bool_t accepted = FALSE;
    (void) core;
    if (reply->generation == event->generation && !event->stopped && !event->completed &&
        (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_RETURN
         || !dbus_message_get_args(message, NULL, DBUS_TYPE_BOOLEAN, &accepted, DBUS_TYPE_INVALID) || !accepted)) {
        if (event->timer) g_source_remove(event->timer);
        event->timer = g_idle_add(complete_event, event);
    }
    unref_event(event); /* Callback owns a reference independently of request. */
    g_free(reply);
}
static int can_handle(NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    return category_for(n_request_get_name(request)) != NULL;
}
static int prepare(NSinkInterface *iface, NRequest *request)
{
    InariEvent *event = g_new0(InariEvent, 1);
    event->references = 1;
    event->iface = iface;
    event->request = request;
    event->cookie = n_request_get_id(request);
    event->category = category_for(n_request_get_name(request));
    event->repeating = n_proplist_get_bool(n_request_get_properties(request), "sound.repeat");
    const char *pattern = n_proplist_get_string(n_request_get_properties(request), "mce.led_pattern");
    if (!event->repeating && pattern && *pattern) event->mce_pattern = g_strdup(pattern);
    n_request_store_data(request, DATA_KEY, event);
    active = g_list_prepend(active, event);
    n_sink_interface_synchronize(iface, request);
    return TRUE;
}
static int play(NSinkInterface *iface, NRequest *request)
{
    InariEvent *event = n_request_get_data(request, DATA_KEY);
    DBusMessage *message = event_message(event, TRUE);
    StartReply *reply = NULL;
    (void) iface;
    ++event->generation;
    event->paused = FALSE;
    event->stop_sent = FALSE;
    event->completed = FALSE;
    if (message) {
        reply = g_new0(StartReply, 1);
        reply->event = event;
        reply->generation = event->generation;
        event->references++;
        event->start_sent = n_dbus_async_call_full(n_sink_interface_get_core(event->iface), start_reply, reply, DBUS_BUS_SESSION, message);
        if (!event->start_sent) {
            unref_event(event);
            g_free(reply);
            reply = NULL;
        }
    } else {
        event->start_sent = FALSE;
    }
    if (message) dbus_message_unref(message);
    if (!event->start_sent) {
        event->timer = g_idle_add(complete_event, event);
    } else if (!event->repeating && !event->mce_pattern) {
        /* Requests without a persistent main-LED pattern remain finite.
         * MCE-backed notifications instead follow that pattern until its
         * deactivation signal or explicit request cancellation. */
        event->timer = g_timeout_add(TRANSIENT_MS, complete_event, event);
    }
    return TRUE;
}
static int pause_event(NSinkInterface *iface, NRequest *request)
{
    InariEvent *event = n_request_get_data(request, DATA_KEY);
    (void) iface;
    ++event->generation;
    event->paused = TRUE;
    if (event->timer) { g_source_remove(event->timer); event->timer = 0; }
    stop_event(event);
    return TRUE;
}
static void stop(NSinkInterface *iface, NRequest *request)
{
    InariEvent *event = n_request_get_data(request, DATA_KEY);
    (void) iface;
    if (!event) return;
    n_request_store_data(request, DATA_KEY, NULL);
    ++event->generation;
    event->stopped = TRUE;
    if (event->timer) { g_source_remove(event->timer); event->timer = 0; }
    stop_event(event);
    active = g_list_remove(active, event);
    unref_event(event);
}
/* Defer completion: ngfd may synchronously stop/free a request when its
 * final sink completes. Scheduling also lets stop/pause cancel this work. */
static void queue_completion(InariEvent *event)
{
    ++event->generation;
    if (event->timer) g_source_remove(event->timer);
    event->timer = g_idle_add(complete_event, event);
}
static DBusHandlerResult mce_pattern_deactivated(NCore *core, DBusConnection *connection,
                                                DBusMessage *message, void *data)
{
    const char *pattern;
    GList *item;
    (void) core; (void) connection; (void) data;
    if (!dbus_message_is_signal(message, MCE_SIGNAL_IF, MCE_LED_PATTERN_DEACTIVATED_SIG)
        || !dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &pattern, DBUS_TYPE_INVALID))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    for (item = active; item; item = item->next) {
        InariEvent *event = item->data;
        if (event->start_sent && !event->stopped && !event->paused && !event->completed
            && event->mce_pattern && !strcmp(event->mce_pattern, pattern))
            queue_completion(event);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
static DBusHandlerResult owner_changed(NCore *core, DBusConnection *connection, DBusMessage *message, void *data)
{
    const char *name, *old_owner, *new_owner;
    GList *item;
    (void) core; (void) connection; (void) data;
    if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)
        || (g_strcmp0(name, SERVICE) && g_strcmp0(name, MCE_SERVICE))
        || !*old_owner || !strcmp(old_owner, new_owner))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    for (item = active; item; item = item->next) {
        InariEvent *event = item->data;
        if (event->stopped || event->completed) continue;
        if (!strcmp(name, MCE_SERVICE)) {
            if (!event->mce_pattern || event->paused || !event->start_sent) continue;
        } else {
            /* The old player lost the event; do not send to its successor. */
            event->stop_sent = TRUE;
        }
        queue_completion(event);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
N_PLUGIN_LOAD(plugin)
{
    static const NSinkInterfaceDecl decl = {
        .name = "inari-blue", .type = N_SINK_INTERFACE_TYPE_LEDS,
        .can_handle = can_handle, .prepare = prepare, .play = play,
        .pause = pause_event, .stop = stop
    };
    NCore *core = n_plugin_get_core(plugin);
    if (!n_dbus_add_match(core, owner_changed, NULL, DBUS_BUS_SESSION,
                         "org.freedesktop.DBus", "/org/freedesktop/DBus", "NameOwnerChanged")
        || !n_dbus_add_match(core, owner_changed, NULL, DBUS_BUS_SYSTEM,
                            "org.freedesktop.DBus", "/org/freedesktop/DBus", "NameOwnerChanged")
        || !n_dbus_add_match(core, mce_pattern_deactivated, NULL, DBUS_BUS_SYSTEM,
                            MCE_SIGNAL_IF, MCE_SIGNAL_PATH, MCE_LED_PATTERN_DEACTIVATED_SIG)) {
        n_dbus_remove_match_by_cb(core, owner_changed);
        n_dbus_remove_match_by_cb(core, mce_pattern_deactivated);
        return FALSE;
    }
    n_plugin_register_sink(plugin, &decl);
    return TRUE;
}
N_PLUGIN_UNLOAD(plugin)
{
    n_dbus_remove_match_by_cb(n_plugin_get_core(plugin), owner_changed);
    n_dbus_remove_match_by_cb(n_plugin_get_core(plugin), mce_pattern_deactivated);
}

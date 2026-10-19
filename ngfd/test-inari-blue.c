#include <check.h>
#include <dbus/dbus.h>
#include <glib.h>
#include <string.h>

/* Include the plugin so its private lifecycle helpers can be exercised with
 * the ngfd and transport entry points replaced by the mocks below. */
#include "plugin.c"

static guint complete_count;
static guint stop_call_count;
static void *request_data;
static gboolean fail_send;
static gboolean repeat_property;
static const char *pattern_property;

typedef struct {
    NDBusReplyFunc callback;
    void *userdata;
} PendingCall;

static GList *pending_calls;

NCore *n_sink_interface_get_core(NSinkInterface *iface)
{
    (void) iface;
    return NULL;
}

void n_sink_interface_complete(NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;
    ++complete_count;
}

gboolean n_dbus_async_call_full(NCore *core, NDBusReplyFunc callback,
                                void *userdata, DBusBusType type,
                                DBusMessage *message)
{
    (void) core;
    (void) type;
    (void) message;
    if (fail_send) return FALSE;
    if (callback) {
        PendingCall *pending = g_new0(PendingCall, 1);
        pending->callback = callback;
        pending->userdata = userdata;
        pending_calls = g_list_append(pending_calls, pending);
    } else {
        ++stop_call_count;
    }
    return TRUE;
}

static void deliver_pending(DBusMessage *reply)
{
    GList *item = pending_calls;
    PendingCall *pending;

    ck_assert(item != NULL);
    pending = item->data;
    pending_calls = g_list_delete_link(pending_calls, item);
    pending->callback(NULL, reply, pending->userdata);
    g_free(pending);
}

const char *n_request_get_name(NRequest *request)
{
    (void) request;
    return "chat_exists";
}

unsigned int n_request_get_id(NRequest *request)
{
    (void) request;
    return 7;
}

const NProplist *n_request_get_properties(NRequest *request)
{
    (void) request;
    return NULL;
}

gboolean n_proplist_get_bool(const NProplist *properties, const char *key)
{
    (void) properties;
    (void) key;
    return repeat_property;
}

const char *n_proplist_get_string(const NProplist *properties, const char *key)
{
    (void) properties; (void) key;
    return pattern_property;
}

void n_request_store_data(NRequest *request, const char *key, void *data)
{
    (void) request;
    (void) key;
    request_data = data;
}

void *n_request_get_data(NRequest *request, const char *key)
{
    (void) request;
    (void) key;
    return request_data;
}

void n_sink_interface_synchronize(NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;
}

NCore *n_plugin_get_core(NPlugin *plugin)
{
    (void) plugin;
    return NULL;
}

guint n_dbus_add_match(NCore *core, NDBusFilterFunc callback, void *userdata,
                       DBusBusType type, const char *iface, const char *path,
                       const char *member)
{
    (void) core; (void) callback; (void) userdata; (void) type;
    (void) iface; (void) path; (void) member;
    return 1;
}

void n_plugin_register_sink(NPlugin *plugin, const NSinkInterfaceDecl *decl)
{
    (void) plugin;
    (void) decl;
}

void n_dbus_remove_match_by_cb(NCore *core, NDBusFilterFunc callback)
{
    (void) core;
    (void) callback;
}

static InariEvent *new_event(void)
{
    InariEvent *event = g_new0(InariEvent, 1);
    event->references = 1;
    event->cookie = 7;
    event->category = "message";
    event->iface = NULL;
    event->request = NULL;
    return event;
}

START_TEST(test_categories_are_bounded)
{
    ck_assert(strcmp(category_for("sms"), "message") == 0);
    ck_assert(strcmp(category_for("chat_exists"), "chat") == 0);
    ck_assert(strcmp(category_for("voip_ringtone"), "internet-call") == 0);
    ck_assert(strcmp(category_for("general_warning"), "other") == 0);
    ck_assert(category_for("battery_low") == NULL);
    ck_assert(category_for("touchscreen") == NULL);
}
END_TEST

START_TEST(test_pause_cancels_old_completion_before_replay)
{
    InariEvent *event = new_event();
    DBusMessage *rejected = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_bool_t accepted = FALSE;
    NRequest *request = (NRequest *) event;
    request_data = event;
    complete_count = 0;
    stop_call_count = 0;

    ck_assert(rejected != NULL);
    ck_assert(dbus_message_append_args(rejected, DBUS_TYPE_BOOLEAN, &accepted,
                                       DBUS_TYPE_INVALID));
    ck_assert(play(NULL, request) == TRUE);
    ck_assert(pending_calls != NULL);

    ck_assert(pause_event(NULL, request) == TRUE);
    ck_assert(stop_call_count == 1);
    ck_assert(event->timer == 0);

    /* A delayed rejection from the old generation must not complete or
     * cancel the timer belonging to the replayed request. */
    ck_assert(play(NULL, request) == TRUE);
    ck_assert(pending_calls != NULL);
    {
        guint current_timer = event->timer;
        deliver_pending(rejected);
        while (g_main_context_iteration(NULL, FALSE)) {}
        ck_assert(event->timer == current_timer);
    }
    ck_assert(complete_count == 0);
    stop(NULL, request);
    deliver_pending(rejected);
    dbus_message_unref(rejected);
}
END_TEST

START_TEST(test_owner_disappearance_completes_active_event)
{
    InariEvent *event = new_event();
    DBusMessage *signal = dbus_message_new_signal("/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus",
                                                  "NameOwnerChanged");
    const char *name = SERVICE;
    const char *old_owner = ":1.9";
    const char *new_owner = "";
    active = g_list_append(NULL, event);
    complete_count = 0;

    ck_assert(signal != NULL);
    ck_assert(dbus_message_append_args(signal,
                                       DBUS_TYPE_STRING, &name,
                                       DBUS_TYPE_STRING, &old_owner,
                                       DBUS_TYPE_STRING, &new_owner,
                                       DBUS_TYPE_INVALID));
    owner_changed(NULL, NULL, signal, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 1);
    ck_assert(event->completed);
    active = g_list_remove(active, event);
    g_free(event);
    dbus_message_unref(signal);
}
END_TEST

START_TEST(test_finite_completion_stops_once)
{
    InariEvent *event = new_event();
    event->start_sent = TRUE;
    complete_count = 0;
    stop_call_count = 0;

    complete_event(event);
    complete_event(event);

    ck_assert(stop_call_count == 1);
    ck_assert(complete_count == 1);
    ck_assert(event->stop_sent);
    ck_assert(event->completed);
    g_free(event);
}
END_TEST

START_TEST(test_start_reply_after_stop_does_not_complete)
{
    InariEvent *event = new_event();
    DBusMessage *reply = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_bool_t accepted = TRUE;
    NRequest *request = (NRequest *) event;
    request_data = event;
    complete_count = 0;

    ck_assert(reply != NULL);
    ck_assert(dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &accepted,
                                       DBUS_TYPE_INVALID));
    ck_assert(play(NULL, request) == TRUE);
    stop(NULL, request);
    deliver_pending(reply);

    ck_assert(complete_count == 0);
    dbus_message_unref(reply);
}
END_TEST

START_TEST(test_rejected_start_completes_asynchronously)
{
    InariEvent *event = new_event();
    DBusMessage *reply = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_bool_t accepted = FALSE;
    NRequest *request = (NRequest *) event;
    request_data = event;
    complete_count = 0;
    stop_call_count = 0;

    ck_assert(reply != NULL);
    ck_assert(dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &accepted,
                                       DBUS_TYPE_INVALID));
    ck_assert(play(NULL, request) == TRUE);
    deliver_pending(reply);
    while (g_main_context_iteration(NULL, FALSE)) {}

    ck_assert(complete_count == 1);
    ck_assert(stop_call_count == 1);
    stop(NULL, request);
    dbus_message_unref(reply);
}
END_TEST

START_TEST(test_send_failure_preserves_request_reference)
{
    InariEvent *event = new_event();
    NRequest *request = (NRequest *) event;
    request_data = event;
    complete_count = 0;
    fail_send = TRUE;
    ck_assert(play(NULL, request) == TRUE);
    ck_assert(event->references == 1);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 1);
    stop(NULL, request);
    fail_send = FALSE;
}
END_TEST

static DBusMessage *pattern_signal(const char *pattern)
{
    DBusMessage *signal = dbus_message_new_signal(MCE_SIGNAL_PATH, MCE_SIGNAL_IF,
                                                 MCE_LED_PATTERN_DEACTIVATED_SIG);
    ck_assert(dbus_message_append_args(signal, DBUS_TYPE_STRING, &pattern, DBUS_TYPE_INVALID));
    return signal;
}

static void finish_pending(void)
{
    dbus_bool_t accepted = TRUE;
    DBusMessage *reply = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);
    ck_assert(dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &accepted, DBUS_TYPE_INVALID));
    while (pending_calls) deliver_pending(reply);
    dbus_message_unref(reply);
}

START_TEST(test_mce_pattern_has_no_six_second_timeout)
{
    InariEvent *event;
    pattern_property = "PatternCommunicationIM";
    prepare(NULL, NULL);
    event = request_data;
    ck_assert_str_eq(event->mce_pattern, pattern_property);
    play(NULL, NULL);
    finish_pending();
    ck_assert(event->timer == 0);
    ck_assert(!event->completed);
    stop(NULL, NULL);
}
END_TEST

START_TEST(test_repeating_alarm_ignores_mce_deactivation)
{
    DBusMessage *signal = pattern_signal("PatternCommunicationIM");
    InariEvent *event;
    pattern_property = "PatternCommunicationIM";
    repeat_property = TRUE;
    prepare(NULL, NULL);
    event = request_data;
    ck_assert(event->mce_pattern == NULL);
    play(NULL, NULL);
    finish_pending();
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(event->timer == 0);
    ck_assert(!event->completed);
    stop(NULL, NULL);
    dbus_message_unref(signal);
}
END_TEST

START_TEST(test_deactivation_completes_all_matching_requests_only)
{
    InariEvent *first = new_event(), *second = new_event(), *other = new_event();
    DBusMessage *signal = pattern_signal("PatternCommunicationIM");
    first->mce_pattern = g_strdup("PatternCommunicationIM");
    second->mce_pattern = g_strdup("PatternCommunicationIM");
    other->mce_pattern = g_strdup("PatternCommunicationEmail");
    first->start_sent = second->start_sent = other->start_sent = TRUE;
    active = g_list_append(active, first);
    active = g_list_append(active, second);
    active = g_list_append(active, other);
    complete_count = stop_call_count = 0;
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    ck_assert(complete_count == 0); /* No reentrant completion during iteration. */
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 2);
    ck_assert(stop_call_count == 2);
    ck_assert(!other->completed);
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 2);
    request_data = first; stop(NULL, NULL);
    request_data = second; stop(NULL, NULL);
    request_data = other; stop(NULL, NULL);
    dbus_message_unref(signal);
}
END_TEST

START_TEST(test_pause_and_stop_cancel_queued_mce_completion)
{
    InariEvent *event;
    DBusMessage *signal = pattern_signal("PatternCommunicationIM");
    pattern_property = "PatternCommunicationIM";
    prepare(NULL, NULL);
    event = request_data;
    play(NULL, NULL);
    complete_count = 0;
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    ck_assert(event->timer != 0);
    pause_event(NULL, NULL);
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 0);
    ck_assert(event->timer == 0);
    play(NULL, NULL);
    finish_pending(); /* Earlier start reply must not complete the new play. */
    mce_pattern_deactivated(NULL, NULL, signal, NULL);
    stop(NULL, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 0);
    dbus_message_unref(signal);
}
END_TEST

START_TEST(test_mce_loss_stops_persistent_but_not_transient_events)
{
    InariEvent *persistent = new_event(), *transient = new_event();
    DBusMessage *signal = dbus_message_new_signal("/org/freedesktop/DBus",
                                                 "org.freedesktop.DBus", "NameOwnerChanged");
    const char *name = MCE_SERVICE, *old_owner = ":1.20", *new_owner = "";
    persistent->mce_pattern = g_strdup("PatternCommunicationIM");
    persistent->start_sent = transient->start_sent = TRUE;
    active = g_list_append(active, persistent);
    active = g_list_append(active, transient);
    complete_count = stop_call_count = 0;
    ck_assert(dbus_message_append_args(signal, DBUS_TYPE_STRING, &name,
                                       DBUS_TYPE_STRING, &old_owner,
                                       DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID));
    owner_changed(NULL, NULL, signal, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
    ck_assert(complete_count == 1);
    ck_assert(stop_call_count == 1);
    ck_assert(persistent->completed);
    ck_assert(!transient->completed);
    request_data = persistent; stop(NULL, NULL);
    request_data = transient; stop(NULL, NULL);
    dbus_message_unref(signal);
}
END_TEST

Suite *inari_blue_suite(void)
{
    Suite *suite = suite_create("inari-blue plugin");
    TCase *tc = tcase_create("lifecycle");
    tcase_add_test(tc, test_categories_are_bounded);
    tcase_add_test(tc, test_mce_pattern_has_no_six_second_timeout);
    tcase_add_test(tc, test_repeating_alarm_ignores_mce_deactivation);
    tcase_add_test(tc, test_deactivation_completes_all_matching_requests_only);
    tcase_add_test(tc, test_pause_and_stop_cancel_queued_mce_completion);
    tcase_add_test(tc, test_mce_loss_stops_persistent_but_not_transient_events);
    tcase_add_test(tc, test_send_failure_preserves_request_reference);
    tcase_add_test(tc, test_finite_completion_stops_once);
    tcase_add_test(tc, test_start_reply_after_stop_does_not_complete);
    tcase_add_test(tc, test_rejected_start_completes_asynchronously);
    tcase_add_test(tc, test_pause_cancels_old_completion_before_replay);
    tcase_add_test(tc, test_owner_disappearance_completes_active_event);
    suite_add_tcase(suite, tc);
    return suite;
}

int main(void)
{
    Suite *suite = inari_blue_suite();
    SRunner *runner = srunner_create(suite);
    int failed;
    srunner_run_all(runner, CK_NORMAL);
    failed = srunner_ntests_failed(runner);
    srunner_free(runner);
    return failed == 0 ? 0 : 1;
}

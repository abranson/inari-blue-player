# Inari Blue ngfd sink

The optional `inari-blue` sink sends the lifetime of selected ngfd events to
the session-bus service `org.sailfishos.InariBlue1`:

* `ringtone` -> `ringtone`
* `voip_ringtone` -> `internet-call`
* `sms`, `sms_fg`, and `sms_exists` -> `message`
* `chat`, `chat_fg`, `chat_exists`, and `social` -> `chat`
* `email` and `email_exists` -> `email`
* `clock` -> `clock`
* `calendar` -> `calendar`
* `notification` and `general_warning` -> `other`

`StartEvent(cookie, category)` is sent when the ngfd request starts. The sink
stays active until ngfd stops the request, then sends `StopEvent(cookie)`.
Non-repeating requests with a resolved `mce.led_pattern` follow the main LED's
lifetime. They continue until MCE broadcasts deactivation of that pattern or
ngfd cancels the request (for example when the notification is removed). Every
active matching request completes on deactivation, including overlapping alerts.
No notification text, contacts, or application identifiers are inspected.
Requests without a main-LED pattern still complete after six seconds. MCE loss
completes only MCE-backed Inari events; it cannot leave them stuck indefinitely.
This affects only the Inari sink: audio and vibration are never replayed.
Requests whose resolved event has `sound.repeat` stay active until ngfd stops
them; this covers repeating ringtones and alarms. Pause stops the LED event and
does not schedule a stale completion callback. A missing player, a rejected
start, or a disappearing player owner completes only this sink and does not
affect other sinks. The plugin never auto-activates the player service.

## Standalone build

Requires the public ngfd plugin development headers (`ngf-plugin.pc`), GLib,
D-Bus and MCE development files. Older ngfd development packages omit
`core-dbus.h`; apply `integration/ngfd-devel.patch` and use that corrected
development package. Check is required for the mocked tests.

```
make -C ngfd
make -C ngfd check
make -C ngfd install DESTDIR=/tmp/inari-root LIBDIR=/usr/lib64
```

The plugin resolves ngfd's public symbols in the running daemon. Do not pass the
libtool-only flags from `ngf-plugin.pc` to the compiler. The Makefile uses its
include flags and links GLib/D-Bus normally. The headers are not vendored.

Tests include the plugin source with mocked ngfd/D-Bus entry points. They need no
running daemon or hardware. For cross builds, build `test-inari-blue` and run it
on the matching target; `install-tests` is separate from normal installation.

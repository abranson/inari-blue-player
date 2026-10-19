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
D-Bus and MCE development files. The omitted `core-dbus.h` is bundled unchanged
in `compat/`, so stock ngfd development packages are sufficient. Check is
required for the mocked tests.

```
make -C ngfd
make -C ngfd check
make -C ngfd install DESTDIR=/tmp/inari-root LIBDIR=/usr/lib64
```

The plugin resolves ngfd's public symbols in the running daemon. Do not pass the
libtool-only flags from `ngf-plugin.pc` to the compiler. The Makefile uses its
include flags and links GLib/D-Bus normally. Only `core-dbus.h` is vendored,
from sailfishos/ngfd commit `3372625`, preserving its LGPL-2.1-or-later notice.
It declares existing runtime symbols; no ngfd implementation is bundled. Check
this header against upstream when changing the supported ngfd baseline.
`integration/ngfd-devel.patch` remains an optional upstream packaging suggestion.

Tests include the plugin source with mocked ngfd/D-Bus entry points. They need no
running daemon or hardware. For cross builds, build `test-inari-blue` and run it
on the matching target; `install-tests` is separate from normal installation.

## Package configuration lifecycle

The RPM uses a Python 3 standard-library helper (`python3-base`, already part of
Sailfish) to add `inari-blue` to `[general] plugins-optional` in the effective
configuration: `/etc/ngfd/ngfd.ini` if present, otherwise
`/usr/share/ngfd/ngfd.ini`. It preserves other bytes, permissions, ownership and
extended attributes. It rejects ambiguous duplicate groups/keys and symlinks.

Ownership of additions is recorded under `/var/lib/ngfd-plugin-inari-blue`.
An entry already supplied by the distribution or administrator is not owned or
removed. Final uninstall removes only our addition and preserves later edits;
upgrades keep it enabled. A trigger reapplies it after updates of
`ngfd-settings-sailfish`. Active user-session ngfd services are restarted after
installation or removal; inactive services stay inactive. This briefly interrupts
active feedback. No player assignments or privacy settings are changed.

This deliberately edits a file belonging to the configuration package, so its
RPM verification may report that one file as modified. It does not replace the
system package. An administrator creating/replacing `/etc/ngfd/ngfd.ini` outside
RPM after installation should run
`/usr/libexec/ngfd-plugin-inari-blue-config install` and then its `restart`
action. Custom `NGF_CONF_PATH`/`NGF_USER_CONF_PATH` environments are not managed.

Run `python3 ngfd/test-manage-config.py` for filesystem lifecycle regressions.
The tests use temporary roots and never alter the host configuration or services.
The upstream config patch remains a cleaner permanent-distribution integration:
when the optional entry is present before installation, no helper edit is needed.

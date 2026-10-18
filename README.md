# Inari Blue player

A fresh native SailfishOS service for the three-channel AW2023 Inari Blue cover.
Patterns are data, selected per feedback category in Sounds and feedback.
There is no standalone application, editor, notification watcher, app override,
or contacts access.

## Components

* This repository: Qt 5.6/C++11 playback service, validated JSON pattern catalogue,
  private category assignments, Symbiosis I²C access, platform privacy switch.
* `ui-jolla-settings-system`: Sounds page section after Tones, visible only for
  the detected Inari Blue (vendor 1, product 4).
* `ngfd`: optional Inari Blue LED sink tied to feedback requests.

Patterns live in `/usr/share/inari-blue/patterns` and
`$XDG_DATA_HOME/inari-blue/patterns` (normally `~/.local/share/inari-blue/patterns`).
A valid user file with the same ID overrides the bundled file. The catalogue
reloads after filesystem changes. See [format](docs/pattern-format.md) and
[D-Bus interface](docs/dbus.md).

Settings live independently in `$XDG_CONFIG_HOME/inari-blue/settings.json`.
A new installation defaults to disabled with all categories Off. The old
notifier's configuration and consent are never read or changed.

## Build and test

Use qmake and make for `inari-blue-player.pro`. Tests use
`tests/pattern-tests.pro` and `tests/player-tests.pro`, each in a separate build
directory. The RPM spec also cross-compiles both tests; they must be executed on
the target architecture separately. Host simulation:

```
build/inari-blue-player --simulate --patterns-dir "$PWD/patterns" --config-file /tmp/inari-test-settings.json
```

Run simulation inside a private `dbus-run-session`, never on a device's normal
session. Simulation deliberately substitutes an available cover and clear
privacy state, and cannot access hardware.

## Ownership and deployment

Do not run this player alongside the notifier or `toh-leds.service`. Deployment
must record their initial states, stop the notifier, and mask the stock breathing
service before enabling this user service. Installation alone does not enable or
start it. Preserve the old configuration for rollback; do not uninstall the old
packages until native integration is accepted. Symbiosis must reload its cover
configuration before granting the new executable access.

Privacy is mandatory: an active or unavailable platform privacy service blocks
all output, including previews. Releasing I²C disables the chip and returns the
loan. Cover removal and service owner changes are observed through D-Bus.

## Current implementation limits

Playback currently evaluates the declarative timeline in software, using a
monotonic clock and 10 ms sampling; missed frames do not shift the timeline.
The hardware's autonomous breathing optimisation and suspend-aware software
playback are not yet implemented. Do not claim screen-off/suspend reliability
until these are validated on a device. Linear fades describe PWM values; visible
brightness also depends on the controller and optical bleed.

# Inari Blue native integration

Native SailfishOS feedback for the three-channel AW2023 Inari Blue cover.
This repository owns the player, pattern files, native Settings page,
privacy control and ngfd notification plugin. There is no standalone application,
pattern editor, notification watcher, app override or contacts access.

## Source layout

| Directory | Purpose |
| --- | --- |
| `src/` | Qt 5.6/C++11 player, JSON pattern engine, hardware access and D-Bus API |
| `patterns/` | Bundled declarative patterns |
| `settings/` | Settings entry and page, ringtone-style picker, previews and file import |
| `data/` | Service activation, cover permissions and privacy control |
| `ngfd/` | Independently built notification sink and its mocked tests |
| `tests/` | Player, pattern, hardware, keepalive and D-Bus tests |
| `rpm/` | Player and standalone ngfd plugin package definitions |
| `integration/` | Small patches and baseline information for existing Sailfish components |

All Inari-specific implementation lives here. Settings discovers our package's
entry manifest and loads the independent Inari Blue page. No Settings source
changes or private audio components are required. The page includes its own
privacy option; the optional central Privacy settings hook exposes the same
setting. The plugin RPM manages its optional-plugin configuration entry on
installation and removal. A bundled upstream header allows builds against stock
ngfd development packages. No patched system packages are needed. See
[integration and package transition](integration/README.md).

## Patterns and configuration

Patterns live in `/usr/share/inari-blue/patterns` and
`$XDG_DATA_HOME/inari-blue/patterns` (normally `~/.local/share/inari-blue/patterns`).
A valid user file with the same ID overrides the bundled file. The catalogue
reloads after filesystem changes. See [format](docs/pattern-format.md) and
[D-Bus interface](docs/dbus.md).

Settings live independently in `$XDG_CONFIG_HOME/inari-blue/settings.json`.
A new installation defaults to disabled with all categories set to None (the
reserved pattern ID is `off`). Notification categories assigned None do not
create a player event. The old notifier's configuration is never imported.

## Build and test

The player uses `qmake inari-blue-player.pro` and `make`. Each test project under
`tests/` builds in a separate directory; see [tests](tests/README.md).
The RPM spec cross-compiles pattern, player, hardware and keepalive tests for
execution on the target architecture.

The ngfd plugin uses `make -C ngfd` and the installed public `ngf-plugin` headers;
it does not require an ngfd source checkout. Run its tests with
`make -C ngfd check`. Cross-compiled tests must run on the target architecture.
See [plugin build instructions](ngfd/README.md).

Build the two RPM specs explicitly from this repository:

* `rpm/inari-blue-player.spec`: player, patterns and QML; separate test subpackage.
* `rpm/ngfd-plugin-inari-blue.spec`: ngfd plugin; separate test subpackage.

Host simulation:

```
build/inari-blue-player --simulate --patterns-dir "$PWD/patterns" --config-file /tmp/inari-test-settings.json
```

Run simulation inside a private `dbus-run-session`, never on a device's normal
session. Simulation substitutes an available cover and clear privacy state, and
cannot access hardware.

## Runtime and ownership

Compatible breathing patterns are lowered to autonomous AW2023 programs.
Other patterns use a monotonic timeline with 10 ms sampling and renewed MCE
keepalive leases; missed frames do not shift the timeline. Linear fades describe
PWM values; visible brightness also depends on the controller and optical bleed.

Do not run this player alongside the old notifier or `toh-leds.service`. Stop
other LED owners and mask the stock breathing service before enabling this user
service. Installation alone does not enable or start it. Symbiosis must reload
its cover configuration before granting the executable access.

The configurable privacy option defaults to enabled. When enabled, active
privacy mode blocks output, including previews. An unavailable privacy service
always blocks output until its state can be read.
Releasing I2C disables the chip and returns the loan. Cover removal and service
owner changes are observed through D-Bus.

The ngfd plugin follows resolved notification categories and main LED lifetime;
repeating alarms and calls follow their feedback request lifetime. It does not
read notification content or change the main LED's behavior.

## Licensing

The native player and new QML use the repository's BSD-3-Clause license.
The extracted ngfd plugin retains its LGPL-2.1-or-later license and notices in
`ngfd/COPYING`. Integration patches retain the licensing of their target projects.

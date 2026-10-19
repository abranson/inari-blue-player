# Optional platform integration

The player and plugin build against stock Sailfish development packages. The
plugin RPM manages its optional-plugin config entry; the player supplies its own
native Settings entry and privacy toggle. No modified system packages are needed.

Three upstream improvements could replace these standalone arrangements:

* ngfd development packaging could install its existing `core-dbus.h`, allowing
  us to stop bundling the LGPL header in `ngfd/compat/`.
* Sailfish feedback configuration could permanently list `inari-blue` among
  optional plugins. The package helper preserves entries already supplied by
  the distribution and would then have nothing to add or remove.
* Central Privacy switch settings could optionally load the player's shared
  `PrivacySwitchItem.qml`. The player's own page already provides this setting;
  the central entry needs no changes to the privacy service or its public API.

Patch proposals targeting other repositories are local working material, excluded
from this repository. In particular, proprietary target-source context is not
needed in the distributable implementation. Dependencies keep their own licences.

## Provenance and compatibility

The bundled D-Bus header comes unchanged from sailfishos/ngfd revision `3372625`,
with its original LGPL-2.1-or-later notice. All other ngfd headers come from the
stock development package. The plugin uses symbols already exported by ngfd.

The fresh player, patterns and privacy component originated in this repository.
The new Settings components and plugin were initially developed in local system
repository branches before consolidation here. The independent pattern row uses
exported Sailfish components and needs no private Settings implementation.

## Transition from experimental packages

Earlier experimental ngfd packages owned `libngfd_inari-blue.so` themselves.
Replace those with a compatible normal ngfd package when installing the standalone
plugin; never force overlapping file ownership. Earlier Settings experiments also
owned Inari controls inside Sounds. Those require a one-time cleanup preserving
any unrelated local Settings changes. Later Inari updates need no Settings rebuild.

An existing config entry from an experimental system package counts as pre-existing:
our helper deliberately preserves it on uninstall. To migrate to package-managed
configuration, first restore the normal system configuration package, preserving
administrator changes, and then install/reinstall the standalone plugin. Do not
blindly downgrade unrelated platform packages or reset user settings.

See [plugin lifecycle](../ngfd/README.md#package-configuration-lifecycle) for
configuration ownership, update triggers and service reload behaviour.

Name:       inari-blue-player
Version:    0.1.0
Release:    8
Summary:    Native Inari Blue pattern playback service
License:    BSD-3-Clause
URL:        https://github.com/abranson/inari-blue-player
Source0:    %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Test)
BuildRequires: pkgconfig(ngf-plugin)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(check)
Requires: symbiosis
Requires: systemd

%description
Plays declarative three-channel LED patterns through Symbiosis hardware access.
Provides a standalone native Settings page for category selection and previews.

%package tests
Summary:    Pattern and player lifecycle tests
Requires:   %{name} = %{version}-%{release}

%description tests
Hardware-free parser and simulated playback regression tests.

%package -n ngfd-plugin-inari-blue
Summary:    Inari Blue sink plugin for ngfd
License:    LGPLv2+
Requires:   ngfd
Requires:   python3-base
Requires:   /usr/sbin/runuser
Requires:   systemd
Requires(posttrans): python3-base
Requires(preun): python3-base
Requires(postun): /usr/sbin/runuser
Requires(postun): systemd

%description -n ngfd-plugin-inari-blue
Optional ngfd sink plugin that mirrors selected feedback events to the
Inari Blue pattern playback service.

%package -n ngfd-plugin-inari-blue-tests
Summary:    Tests for the Inari Blue ngfd sink plugin
License:    LGPLv2+
Requires:   ngfd-plugin-inari-blue = %{version}-%{release}

%description -n ngfd-plugin-inari-blue-tests
Mocked lifecycle tests and isolated configuration regressions for the plugin.

%prep
%setup -q

%build
%qmake5
%make_build
mkdir -p build-sdk-tests build-sdk-player-tests
(cd build-sdk-tests && %qmake5 ../tests/pattern-tests.pro && make clean && %make_build)
(cd build-sdk-player-tests && %qmake5 ../tests/player-tests.pro && make clean && %make_build)
mkdir -p build-sdk-hardware-tests build-sdk-keepalive-tests
(cd build-sdk-hardware-tests && %qmake5 ../tests/hardware-tests.pro && make clean && %make_build)
(cd build-sdk-keepalive-tests && %qmake5 ../tests/keepalive-tests.pro && make clean && %make_build)

%make_build -C ngfd all test-inari-blue

%install
%qmake5_install
install -D -m755 build-sdk-tests/pattern-tests %{buildroot}/opt/tests/inari-blue-player/pattern-tests
install -D -m755 build-sdk-player-tests/player-tests %{buildroot}/opt/tests/inari-blue-player/player-tests
install -D -m755 build-sdk-hardware-tests/hardware-tests %{buildroot}/opt/tests/inari-blue-player/hardware-tests
install -D -m755 build-sdk-keepalive-tests/keepalive-tests %{buildroot}/opt/tests/inari-blue-player/keepalive-tests

%make_install -C ngfd PREFIX=%{_prefix} LIBDIR=%{_libdir} \
    PLUGIN_DIR=%{_libdir}/ngf
%make_build -C ngfd install-tests DESTDIR=%{buildroot} PREFIX=%{_prefix} \
    LIBDIR=%{_libdir} PLUGIN_DIR=%{_libdir}/ngf \
    TESTDIR=/opt/tests/ngfd-plugin-inari-blue
install -d -m 0700 %{buildroot}%{_localstatedir}/lib/ngfd-plugin-inari-blue

%files
%license LICENSE
%doc README.md docs/pattern-format.md docs/dbus.md
%{_libexecdir}/inari-blue-player
%{_userunitdir}/inari-blue-player.service
%{_datadir}/dbus-1/services/org.sailfishos.InariBlue1.service
%dir %{_datadir}/inari-blue
%{_datadir}/inari-blue/patterns
%{_datadir}/inari-blue/PrivacySwitchItem.qml
%{_datadir}/inari-blue/settings
%{_datadir}/jolla-settings/entries/inari-blue.json
%{_datadir}/tohd-1/tohs/0001/0004/inari-player.yaml

%files tests
/opt/tests/inari-blue-player

# Install/upgrade after all transaction payloads are present. The helper records
# only entries it adds; existing distribution/admin entries remain unowned.
%posttrans -n ngfd-plugin-inari-blue
%{_libexecdir}/ngfd-plugin-inari-blue-config install && \
    %{_libexecdir}/ngfd-plugin-inari-blue-config restart

# A distribution configuration update may replace the list we edited.
%triggerpostun -n ngfd-plugin-inari-blue -- ngfd-settings-sailfish
if [ "$1" -gt 0 ] && [ "$2" -gt 0 ]; then
    %{_libexecdir}/ngfd-plugin-inari-blue-config refresh && \
        %{_libexecdir}/ngfd-plugin-inari-blue-config restart
fi

%preun -n ngfd-plugin-inari-blue
if [ "$1" -eq 0 ]; then
    %{_libexecdir}/ngfd-plugin-inari-blue-config remove || exit 1
fi

# On upgrade posttrans reloads the new plugin. On final erase the helper has
# already been removed, so reload active user services directly here.
%postun -n ngfd-plugin-inari-blue
if [ "$1" -eq 0 ]; then
    for runtime in /run/user/[0-9]*; do
        [ -d "$runtime" ] || continue
        uid=${runtime##*/}
        case "$uid" in *[!0-9]*|'') continue ;; esac
        user=$(getent passwd "$uid" | cut -d: -f1)
        [ -n "$user" ] || continue
        for bus in "$runtime/bus" "$runtime/dbus/user_bus_socket"; do
            [ -S "$bus" ] || continue
            /usr/sbin/runuser -u "$user" -- /usr/bin/env \
                XDG_RUNTIME_DIR="$runtime" DBUS_SESSION_BUS_ADDRESS="unix:path=$bus" \
                /usr/bin/systemctl --user try-restart ngfd.service || \
                echo "Inari Blue: restart ngfd or log in again" >&2
            break
        done
    done
fi

%files -n ngfd-plugin-inari-blue
%license ngfd/COPYING
%doc ngfd/README.md
%{_libdir}/ngf/libngfd_inari-blue.so
%{_libexecdir}/ngfd-plugin-inari-blue-config
%attr(0700,root,root) %dir %{_localstatedir}/lib/ngfd-plugin-inari-blue
%attr(0600,root,root) %ghost %{_localstatedir}/lib/ngfd-plugin-inari-blue/managed.json

%files -n ngfd-plugin-inari-blue-tests
/opt/tests/ngfd-plugin-inari-blue/test-inari-blue
/opt/tests/ngfd-plugin-inari-blue/test-manage-config.py

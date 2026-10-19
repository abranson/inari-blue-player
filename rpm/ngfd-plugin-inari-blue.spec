Name:       ngfd-plugin-inari-blue
Version:    0.1
Release:    2
Summary:    Inari Blue sink plugin for ngfd
License:    LGPLv2+
Source0:    %{name}-%{version}.tar.gz
Requires:   ngfd
Requires:   python3-base
Requires:   /usr/sbin/runuser
Requires:   systemd
Requires(posttrans): python3-base
Requires(preun): python3-base
Requires(postun): /usr/sbin/runuser
Requires(postun): systemd
BuildRequires: pkgconfig(ngf-plugin)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(mce)
BuildRequires: pkgconfig(check)

%description
Optional ngfd sink plugin that mirrors selected feedback events to the
Inari Blue pattern playback service.

%package tests
Summary:    Tests for the Inari Blue ngfd sink plugin
Requires:   %{name} = %{version}-%{release}

%description tests
Mocked lifecycle tests for the Inari Blue ngfd sink plugin.

%prep
%setup -q

%build
%make_build -C ngfd all test-inari-blue

%install
%make_install -C ngfd PREFIX=%{_prefix} LIBDIR=%{_libdir} \
    PLUGIN_DIR=%{_libdir}/ngf
%make_build -C ngfd install-tests DESTDIR=%{buildroot} PREFIX=%{_prefix} \
    LIBDIR=%{_libdir} PLUGIN_DIR=%{_libdir}/ngf \
    TESTDIR=/opt/tests/ngfd-plugin-inari-blue
install -d -m 0700 %{buildroot}%{_localstatedir}/lib/ngfd-plugin-inari-blue

# Install/upgrade after all transaction payloads are present. The helper records
# only entries it adds; existing distribution/admin entries remain unowned.
%posttrans
%{_libexecdir}/ngfd-plugin-inari-blue-config install && \
    %{_libexecdir}/ngfd-plugin-inari-blue-config restart

# A distribution configuration update may replace the list we edited.
%triggerpostun -- ngfd-settings-sailfish
if [ "$1" -gt 0 ] && [ "$2" -gt 0 ]; then
    %{_libexecdir}/ngfd-plugin-inari-blue-config refresh && \
        %{_libexecdir}/ngfd-plugin-inari-blue-config restart
fi

%preun
if [ "$1" -eq 0 ]; then
    %{_libexecdir}/ngfd-plugin-inari-blue-config remove || exit 1
fi

# On upgrade posttrans reloads the new plugin. On final erase the helper has
# already been removed, so reload active user services directly here.
%postun
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

%files
%license ngfd/COPYING
%doc ngfd/README.md
%{_libdir}/ngf/libngfd_inari-blue.so
%{_libexecdir}/ngfd-plugin-inari-blue-config
%attr(0700,root,root) %dir %{_localstatedir}/lib/ngfd-plugin-inari-blue
%attr(0600,root,root) %ghost %{_localstatedir}/lib/ngfd-plugin-inari-blue/managed.json

%files tests
/opt/tests/ngfd-plugin-inari-blue/test-inari-blue
/opt/tests/ngfd-plugin-inari-blue/test-manage-config.py

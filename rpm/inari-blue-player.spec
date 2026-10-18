Name:       inari-blue-player
Version:    0.1.0
Release:    2
Summary:    Native Inari Blue pattern playback service
License:    BSD-3-Clause
Source0:    %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Test)
Requires: symbiosis
Requires: systemd

%description
Plays declarative three-channel LED patterns through Symbiosis hardware access.
Provides category selection and preview services for native Sounds settings.

%package tests
Summary:    Pattern and player lifecycle tests
Requires:   %{name} = %{version}-%{release}

%description tests
Hardware-free parser and simulated playback regression tests.

%prep
%setup -q

%build
%qmake5
%make_build
mkdir -p build-tests build-player-tests
(cd build-tests && %qmake5 ../tests/pattern-tests.pro && %make_build)
(cd build-player-tests && %qmake5 ../tests/player-tests.pro && %make_build)
mkdir -p build-hardware-tests build-keepalive-tests
(cd build-hardware-tests && %qmake5 ../tests/hardware-tests.pro && %make_build)
(cd build-keepalive-tests && %qmake5 ../tests/keepalive-tests.pro && %make_build)

%install
%qmake5_install
install -D -m755 build-tests/pattern-tests %{buildroot}/opt/tests/inari-blue-player/pattern-tests
install -D -m755 build-player-tests/player-tests %{buildroot}/opt/tests/inari-blue-player/player-tests
install -D -m755 build-hardware-tests/hardware-tests %{buildroot}/opt/tests/inari-blue-player/hardware-tests
install -D -m755 build-keepalive-tests/keepalive-tests %{buildroot}/opt/tests/inari-blue-player/keepalive-tests

%files
%license LICENSE
%doc README.md docs/pattern-format.md docs/dbus.md
%{_libexecdir}/inari-blue-player
%{_userunitdir}/inari-blue-player.service
%{_datadir}/dbus-1/services/org.sailfishos.InariBlue1.service
%dir %{_datadir}/inari-blue
%{_datadir}/inari-blue/patterns
%{_datadir}/tohd-1/tohs/0001/0004/inari-player.yaml

%files tests
/opt/tests/inari-blue-player

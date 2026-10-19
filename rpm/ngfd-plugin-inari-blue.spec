Name:       ngfd-plugin-inari-blue
Version:    0.1
Release:    1
Summary:    Inari Blue sink plugin for ngfd
License:    LGPLv2+
Source0:    %{name}-%{version}.tar.gz
Requires:   ngfd
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

%files
%license ngfd/COPYING
%doc ngfd/README.md
%{_libdir}/ngf/libngfd_inari-blue.so

%files tests
/opt/tests/ngfd-plugin-inari-blue/test-inari-blue

Name:       voicecall-qt5
Summary:    Dialer engine for Nemo Mobile
Version:    0.7.14
Release:    1
License:    ASL 2.0 and GPLv2+ and LGPLv2+ and BSD
URL:        https://github.com/sailfishos/voicecall
Source0:    %{name}-%{version}.tar.bz2
Source1:    %{name}.privileges
Requires:   systemd
Requires:   systemd-user-session-targets
Requires:   voicecall-qt5-plugin-telepathy = %{version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(libresourceqt5)
BuildRequires:  pkgconfig(libpulse-mainloop-glib)
BuildRequires:  pkgconfig(ngf-qt5)
BuildRequires:  pkgconfig(commhistory-qt5) >= 1.12.6
BuildRequires:  pkgconfig(qt5-boostable)
BuildRequires:  pkgconfig(nemodevicelock)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  oneshot
%{_oneshot_requires_post}

%description
%{summary}.

%package devel
Summary:    Voicecall development package
Requires:   %{name} = %{version}-%{release}

%description devel
%{summary}.

%package plugin-telepathy
Summary:    Voicecall plugin for calls using telepathy
Requires:   %{name} = %{version}-%{release}
Conflicts:  voicecall-qt5-plugin-ofono
BuildRequires:  pkgconfig(TelepathyQt5)
BuildRequires:  pkgconfig(TelepathyQt5Farstream)

%description plugin-telepathy
%{summary}.

%package plugin-ofono
Summary:    Voicecall plugin for calls using ofono
Requires:   %{name} = %{version}-%{release}
Conflicts:  voicecall-qt5-plugin-telepathy
BuildRequires:  pkgconfig(qofono-qt5)

%description plugin-ofono
%{summary}.

%package plugin-voicecall-filter
Summary:    Voicecall filter plugin
Requires:   %{name} = %{version}-%{release}

%description plugin-voicecall-filter
A voicecall filter plugin using DConf.

%package plugin-voicecall-filter-devel
Summary:    Voicecall filter plugin development package
Requires:   %{name} = %{version}-%{release}

%description plugin-voicecall-filter-devel
Development files for %{name}.

%package tests
Summary:    Voicecall test package
Requires:   %{name} = %{version}-%{release}

%description tests
Tests for %{name}.


%prep
%setup -q -n %{name}-%{version}

%build

%qmake5 

qmake -qt=5 CONFIG+=enable-ngf CONFIG+=enable-audiopolicy CONFIG+=enable-telepathy CONFIG+=enable-nemo-devicelock CONFIG+=install-servicefiles "PROJECT_VERSION=%{version}" "PKGCONFIG_LIB=%{_lib}"
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

mkdir -p %{buildroot}%{_userunitdir}/user-session.target.wants
ln -s ../voicecall-manager.service %{buildroot}%{_userunitdir}/user-session.target.wants/

mkdir -p %{buildroot}%{_datadir}/mapplauncherd/privileges.d
install -m 644 -p %{SOURCE1} %{buildroot}%{_datadir}/mapplauncherd/privileges.d/

chmod +x %{buildroot}/%{_oneshotdir}/*

%post
/sbin/ldconfig
if [ "$1" -ge 1 ]; then
systemctl-user daemon-reload || :
systemctl-user restart voicecall-manager.service || :
fi

# run now for sufficient permissions to move to privileged dir
%{_bindir}/add-oneshot --now phone-move-recordings-dir || :

%postun
/sbin/ldconfig
if [ "$1" -eq 0 ]; then
systemctl-user stop voicecall-manager.service || :
systemctl-user daemon-reload || :
fi

%post plugin-voicecall-filter -p /sbin/ldconfig

%postun plugin-voicecall-filter -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license LICENSE.LGPL21 LICENSE.GPL2 LICENSE.ASL2 LICENSE.BSD
%{_libdir}/libvoicecall.so.1
%{_libdir}/libvoicecall.so.1.0
%{_libdir}/libvoicecall.so.1.0.0
%dir %{_libdir}/qt5/qml/org/nemomobile/voicecall
%{_libdir}/qt5/qml/org/nemomobile/voicecall/libvoicecall.so
%{_libdir}/qt5/qml/org/nemomobile/voicecall/qmldir
%{_bindir}/voicecall-manager
%dir %{_libdir}/voicecall
%dir %{_libdir}/voicecall/plugins
%{_libdir}/voicecall/plugins/libvoicecall-playback-manager-plugin.so
%{_libdir}/voicecall/plugins/libvoicecall-ngf-plugin.so
%{_libdir}/voicecall/plugins/libvoicecall-mce-plugin.so
%{_libdir}/voicecall/plugins/libvoicecall-commhistory-plugin.so
%{_userunitdir}/voicecall-manager.service
%{_userunitdir}/user-session.target.wants/voicecall-manager.service
%{_datadir}/mapplauncherd/privileges.d/*
%{_oneshotdir}/phone-move-recordings-dir

%files devel
%defattr(-,root,root,-)
%{_libdir}/libvoicecall.so
%{_includedir}/voicecall

%files plugin-telepathy
%defattr(-,root,root,-)
%{_libdir}/voicecall/plugins/libvoicecall-telepathy-plugin.so

%files plugin-ofono
%defattr(-,root,root,-)
%{_libdir}/voicecall/plugins/libvoicecall-ofono-plugin.so

%files plugin-voicecall-filter
%{_libdir}/libvoicecall-filter.so.*
%{_libdir}/voicecall/plugins/libvoicecall-filter-plugin.so

%files plugin-voicecall-filter-devel
%{_libdir}/libvoicecall-filter.so
%{_includedir}/voicecall/Filter
%{_libdir}/pkgconfig/voicecall-filter.pc

%files tests
/opt/tests/voicecall/filter


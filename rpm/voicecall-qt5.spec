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
BuildRequires:  pkgconfig(qt5-boostable)
BuildRequires:  pkgconfig(nemodevicelock)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  oneshot
%{_oneshot_requires_post}

%description
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

%prep
%setup -q -n %{name}-%{version}

%build

%qmake5 

qmake -qt=5 CONFIG+=enable-ngf CONFIG+=enable-audiopolicy CONFIG+=enable-telepathy CONFIG+=enable-nemo-devicelock CONFIG+=install-servicefiles
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

%files
%defattr(-,root,root,-)
%license LICENSE.LGPL21 LICENSE.GPL2 LICENSE.ASL2 LICENSE.BSD
%{_libdir}/libvoicecall.so.1
%{_libdir}/libvoicecall.so.1.0
%{_libdir}/libvoicecall.so.1.0.0
%exclude %{_libdir}/libvoicecall.so
%dir %{_libdir}/qt5/qml/org/nemomobile/voicecall
%{_libdir}/qt5/qml/org/nemomobile/voicecall/libvoicecall.so
%{_libdir}/qt5/qml/org/nemomobile/voicecall/qmldir
%{_bindir}/voicecall-manager
%dir %{_libdir}/voicecall
%dir %{_libdir}/voicecall/plugins
%{_libdir}/voicecall/plugins/libvoicecall-playback-manager-plugin.so
%{_libdir}/voicecall/plugins/libvoicecall-ngf-plugin.so
%{_libdir}/voicecall/plugins/libvoicecall-mce-plugin.so
%{_userunitdir}/voicecall-manager.service
%{_userunitdir}/user-session.target.wants/voicecall-manager.service
%{_datadir}/mapplauncherd/privileges.d/*
%{_oneshotdir}/phone-move-recordings-dir

%files plugin-telepathy
%defattr(-,root,root,-)
%{_libdir}/voicecall/plugins/libvoicecall-telepathy-plugin.so

%files plugin-ofono
%defattr(-,root,root,-)
%{_libdir}/voicecall/plugins/libvoicecall-ofono-plugin.so


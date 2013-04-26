Name: icinga-iris
Version: 1.1.5
Release: %{_release}
License: Proprietary
Source: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

#####################################################################

Summary: Fast passive service check acceptor for Icinga
Group: Application/System

Requires: icinga

%description
IRIS is an event broker module for Icinga that starts up a daemon thread,
listening on TCP/5667 for passive service check results submitted by the
send_iris utility.  These results are injected directly into Icinga's
processing queues.

This package provides the broker module that runs on the Icinga server.

# Uncomment the following line to get debuginfo builds outside of CI
# (Note: the CI build boxes get angry if they see this uncommented)
#%(REMOVE_ME)debug_package

%package send
Group: Application/System
Summary: Provides the send_nsca utility for submitting check results.

%description send
IRIS is an event broker module for Icinga that starts up a daemon thread,
listening on TCP/5667 for passive service check results submitted by the
send_iris utility.  These results are injected directly into Icinga's
processing queues.
document for more information. 

This package provides the send_iris utility that runs on the client.

%prep
%setup -q

%build
make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT
install -D -m 0755 iris.so ${RPM_BUILD_ROOT}%{_libdir}/icinga/iris.so
install -D -m 0755 send_iris ${RPM_BUILD_ROOT}%{_bindir}/send_iris

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(755,root,root)
%{_libdir}/icinga/iris.so

%files send
%defattr(755,root,root)
%{_bindir}/send_iris

%changelog
* Fri Apr 26 2013 James Hunt <jhunt@synacor.com> 1.1.5-1
- Fix deinit problems, to avoid failure on reload / SIGHUP

* Thu Apr 25 2013 James Hunt <jhunt@synacor.com> 1.1.4-1
- Fix mutex problems with Icinga internals

* Mon Apr 22 2013 James Hunt <jhunt@synacor.com> 1.0.0-1
- Initial package

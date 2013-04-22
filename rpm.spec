Name: icinga-iris
Version: 1.0.0
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
install -D -m 0755 .libs/libiris.so ${RPM_BUILD_ROOT}%{_libdir}/icinga/iris.so
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
* Mon Apr 22 2013 James Hunt <jhunt@synacor.com> 1.0.0-1
- Initial package

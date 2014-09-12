Name: icinga-iris
Version: 1.1.8
Release: %{_release}
License: Proprietary
Source: %{_source0}
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

#####################################################################

Summary: Fast passive service check acceptor for Icinga
Group: Application/System

BuildRequires: gcc
BuildRequires: libtool

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
* Fri Sep 12 2014 James Hunt <jhunt@synacor.com> 1.1.8-1
- Client deadlining to prune stalled connections
- Memory init fixes to make vbalgrind happier

* Thu Jun 05 2014 Dan Molik <dmolik@synacor.com> 1.1.7-12
- Cutover to rpm-builder

* Wed Mar 05 2014 James Hunt <jhunt@synacor.com> 1.1.7-1
- Update error handling from syscalls
- Move over to syslog for all log traffic (ITM-3116)

* Tue May 07 2013 James Hunt <jhunt@synacor.com> 1.1.6-1
- Close client connections and dealloc memory on EVENTLOOPEND (ITM-2350)

* Fri Apr 26 2013 James Hunt <jhunt@synacor.com> 1.1.5-1
- Fix deinit problems, to avoid failure on reload / SIGHUP

* Thu Apr 25 2013 James Hunt <jhunt@synacor.com> 1.1.4-1
- Fix mutex problems with Icinga internals

* Mon Apr 22 2013 James Hunt <jhunt@synacor.com> 1.0.0-1
- Initial package

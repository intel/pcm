#
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#

# norootforbuild

Name:            pcm
Version:         0
Release:         0
Summary:         Processor Counter Monitor
Group:           System/Monitoring
License:         BSD-3-Clause
Url:             https://github.com/opcm/pcm/archive
Source:          master.zip
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
AutoReqProv:    on
BuildRequires:  unzip
BuildRequires:  gcc
BuildRequires:  gcc-c++

%description

Processor Counter Monitor (PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel(r) Core(tm), Xeon(r), Atom(tm) and Xeon Phi(tm) processors. PCM works on Linux, Windows, Mac OS X, FreeBSD and DragonFlyBSD operating systems.
 

%prep
%setup -n pcm-master

%build
make -j 

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{_bindir}
mkdir -p $RPM_BUILD_ROOT/usr/share/pcm

install -s -m 755 pcm-core.x $RPM_BUILD_ROOT/%{_bindir}/pcm-core
install -s -m 755 pcm-iio.x $RPM_BUILD_ROOT/%{_bindir}/pcm-iio
install -s -m 755 pcm-latency.x $RPM_BUILD_ROOT/%{_bindir}/pcm-latency
install -s -m 755 pcm-lspci.x $RPM_BUILD_ROOT/%{_bindir}/pcm-lspci
install -s -m 755 pcm-memory.x $RPM_BUILD_ROOT/%{_bindir}/pcm-memory
install -s -m 755 pcm-msr.x $RPM_BUILD_ROOT/%{_bindir}/pcm-msr
install -s -m 755 pcm-numa.x $RPM_BUILD_ROOT/%{_bindir}/pcm-numa
install -s -m 755 pcm-pcicfg.x $RPM_BUILD_ROOT/%{_bindir}/pcm-pcicfg
install -s -m 755 pcm-pcie.x $RPM_BUILD_ROOT/%{_bindir}/pcm-pcie
install -s -m 755 pcm-power.x $RPM_BUILD_ROOT/%{_bindir}/pcm-power
install -s -m 755 pcm-sensor.x $RPM_BUILD_ROOT/%{_bindir}/pcm-sensor
install -s -m 755 pcm-tsx.x $RPM_BUILD_ROOT/%{_bindir}/pcm-tsx
install -s -m 755 pcm.x $RPM_BUILD_ROOT/%{_bindir}/pcm
install -s -m 755 daemon/client/Debug/client $RPM_BUILD_ROOT/%{_bindir}/pcm-client
install -s -m 755 daemon/daemon/Debug/daemon $RPM_BUILD_ROOT/%{_bindir}/pcm-daemon
install -m 755 pcm-bw-histogram.sh $RPM_BUILD_ROOT/%{_bindir}/pcm-bw-histogram
install -m 644 opCode.txt $RPM_BUILD_ROOT/usr/share/pcm/

%clean
rm -rf $RPM_BUILD_ROOT

%post
%postun

%files
%defattr(-,root,root,0755)
%doc license.txt LINUX_HOWTO.txt
%{_bindir}/pcm-core
%{_bindir}/pcm-iio
%{_bindir}/pcm-latency
%{_bindir}/pcm-lspci
%{_bindir}/pcm-memory
%{_bindir}/pcm-msr
%{_bindir}/pcm-numa
%{_bindir}/pcm-pcicfg
%{_bindir}/pcm-pcie
%{_bindir}/pcm-power
%{_bindir}/pcm-sensor
%{_bindir}/pcm-tsx
%{_bindir}/pcm
%{_bindir}/pcm-client
%{_bindir}/pcm-daemon
%{_bindir}/pcm-bw-histogram
/usr/share/pcm
/usr/share/pcm/opCode.txt

%changelog
* Wed Oct 23 2019 - roman.dementiev@intel.com
	use %{_bindir} and drop executable suffixes
* Mon Oct 21 2019 - roman.dementiev@intel.com
	add opCode file to /usr/share/pcm
	use "install" to copy pcm-bw-histogram.sh
* Fri Oct 18 2019 - roman.dementiev@intel.com
	created spec file

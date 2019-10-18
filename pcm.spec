#
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#

# norootforbuild

Name:            pcm
Version:         X.Y
Release:         0
Summary:         Processor Counter Monitor
Group:           Development/Tools
License:         Open Source Initiative OSI - The BSD License: Licensing
Url:             https://github.com/opcm/pcm/ 
Provides:        pcm
Source:          https://github.com/opcm/pcm/archive/master.zip
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
AutoReqProv:    on
BuildRequires:  unzip

%description

Processor Counter Monitor (PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel(r) Core(tm), Xeon(r), Atom(tm) and Xeon Phi(tm) processors. PCM works on Linux, Windows, Mac OS X, FreeBSD and DragonFlyBSD operating systems.
 

%prep
%setup -n pcm-master

%build
make -j 

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/bin

install -s -m 755 pcm-core.x $RPM_BUILD_ROOT/usr/bin/ 
install -s -m 755 pcm-iio.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-latency.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-lspci.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-memory.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-msr.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-numa.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-pcicfg.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-pcie.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-power.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-sensor.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm-tsx.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 pcm.x $RPM_BUILD_ROOT/usr/bin/
install -s -m 755 daemon/client/Debug/client  $RPM_BUILD_ROOT/usr/bin/pcm-client.x
install -s -m 755 daemon/daemon/Debug/daemon  $RPM_BUILD_ROOT/usr/bin/pcm-daemon.x
cp pcm-bw-histogram.sh $RPM_BUILD_ROOT/usr/bin/pcm-bw-histogram.sh
chmod 755 $RPM_BUILD_ROOT/usr/bin/pcm-bw-histogram.sh

%clean
rm -rf $RPM_BUILD_ROOT

%post
%postun

%files
%defattr(-,root,root,0755)
%doc license.txt LINUX_HOWTO.txt
/usr/bin/pcm-core.x
/usr/bin/pcm-iio.x
/usr/bin/pcm-latency.x
/usr/bin/pcm-lspci.x
/usr/bin/pcm-memory.x
/usr/bin/pcm-msr.x
/usr/bin/pcm-numa.x
/usr/bin/pcm-pcicfg.x
/usr/bin/pcm-pcie.x
/usr/bin/pcm-power.x
/usr/bin/pcm-sensor.x
/usr/bin/pcm-tsx.x
/usr/bin/pcm.x
/usr/bin/pcm-client.x
/usr/bin/pcm-daemon.x
/usr/bin/pcm-bw-histogram.sh

%changelog
* Fri Oct 18 2019 - roman.dementiev@intel.com
	created spec file

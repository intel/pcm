
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
mkdir -p $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT/usr/share/pcm

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
install -m 755 pcm-bw-histogram.sh $RPM_BUILD_ROOT/usr/bin/
install -m 644 opCode.txt $RPM_BUILD_ROOT/usr/share/pcm/

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
/usr/share/pcm
/usr/share/pcm/opCode.txt

%changelog
* Mon Oct 21 2019 - roman.dementiev@intel.com
	add opCode file to /usr/share/pcm
	use "install" to copy pcm-bw-histogram.sh
* Fri Oct 18 2019 - roman.dementiev@intel.com
	created spec file


Name:            pcm
Version:         master
Release:         0
Summary:         Intel(r) Performance Counter Monitor
Group:           System/Monitoring
License:         BSD-3-Clause
Url:             https://github.com/intel/pcm
Source:          %{version}.zip
BuildRoot:       %{_tmppath}/%{name}-%{version}-build
AutoReqProv:     on
BuildRequires:   unzip
BuildRequires:   gcc
BuildRequires:   make
BuildRequires:   gcc-c++
BuildRequires:   cmake
%if 0%{?suse_version}
BuildRequires:   libopenssl-devel
%else
BuildRequires:   openssl-devel
BuildRequires:   libasan
%endif


%description

Intel(r) Performance Counter Monitor (Intel(r) PCM) is an application programming interface (API) and a set of tools based on the API to monitor performance and energy metrics of Intel(r) Core(tm), Xeon(r), Atom(tm) and Xeon Phi(tm) processors. PCM works on Linux, Windows, Mac OS X, FreeBSD and DragonFlyBSD operating systems.

%prep
%setup -n pcm-master

%build
mkdir build
cd build
cmake -DPCM_NO_STATIC_LIBASAN=ON -DCMAKE_INSTALL_PREFIX=/usr/ -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j 

%install
rm -rf $RPM_BUILD_ROOT
cd build
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%post
%postun

%files
%defattr(-,root,root,0755)
%doc LICENSE doc/LINUX_HOWTO.txt
/usr/share/doc/PCM
/usr/share/licenses/pcm
/usr/share/doc/PCM/CUSTOM-COMPILE-OPTIONS.md
/usr/share/doc/PCM/DOCKER_README.md
/usr/share/doc/PCM/ENVVAR_README.md
/usr/share/doc/PCM/FAQ.md
/usr/share/doc/PCM/FREEBSD_HOWTO.txt
/usr/share/doc/PCM/LINUX_HOWTO.txt
/usr/share/doc/PCM/MAC_HOWTO.txt
/usr/share/doc/PCM/PCM-EXPORTER.md
/usr/share/doc/PCM/PCM-SENSOR-SERVER-README.md
/usr/share/doc/PCM/PCM_RAW_README.md
/usr/share/doc/PCM/README.md
/usr/share/doc/PCM/WINDOWS_HOWTO.md
/usr/share/doc/PCM/license.txt
/usr/share/licenses/pcm/LICENSE
%{_sbindir}/pcm-core
%{_sbindir}/pcm-iio
%{_sbindir}/pcm-latency
%{_sbindir}/pcm-lspci
%{_sbindir}/pcm-memory
%{_sbindir}/pcm-msr
%{_sbindir}/pcm-mmio
%{_sbindir}/pcm-tpmi
%{_sbindir}/pcm-numa
%{_sbindir}/pcm-pcicfg
%{_sbindir}/pcm-accel
%{_sbindir}/pcm-pcie
%{_sbindir}/pcm-power
%{_sbindir}/pcm-sensor
%{_sbindir}/pcm-sensor-server
%{_sbindir}/pcm-tsx
%{_sbindir}/pcm-raw
%{_sbindir}/pcm
%{_bindir}/pcm-client
%{_sbindir}/pcm-daemon
%{_sbindir}/pcm-bw-histogram
%{_datadir}/pcm/

%changelog
* Mon Feb 28 2022 - roman.dementiev@intel.com
        add addition doc files
* Tue Jan 04 2022 - maria.markova@intel.com
        Add cmake adaptation
* Fri Dec 17 2021 - maria.markova@intel.com
        Move licence.txt Linix_HOWTO.txt to doc folder
* Tue Aug 25 2020 - roman.dementiev@intel.com
        Add pcm-raw under %files
* Wed Apr 01 2020 - otto.g.bruggeman@intel.com
        Add pcm-sensor-server under %files
* Mon Nov 25 2019 - roman.dementiev@intel.com
        call make install and use _sbindir or _bindir
* Mon Oct 21 2019 - roman.dementiev@intel.com
	add opCode file to /usr/share/pcm
	use "install" to copy pcm-bw-histogram.sh
* Fri Oct 18 2019 - roman.dementiev@intel.com
	created spec file

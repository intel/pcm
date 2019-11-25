
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
make install prefix=$RPM_BUILD_ROOT/%{_bindir}/..

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
%{_bindir}/../share/pcm
%{_bindir}/../share/pcm/opCode.txt

%changelog
* Mon Nov 25 2019 - roman.dementiev@intel.com
        call make install and use %{_bindir}
* Mon Oct 21 2019 - roman.dementiev@intel.com
	add opCode file to /usr/share/pcm
	use "install" to copy pcm-bw-histogram.sh
* Fri Oct 18 2019 - roman.dementiev@intel.com
	created spec file

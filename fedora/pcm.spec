Name:           pcm
Version:        202105
Release:        1%{?dist}
Summary:        Processor Counter Monitor
License:        BSD
Url:            https://github.com/opcm/pcm
Source0:        %{url}/archive/%{version}/%{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
ExclusiveArch:  %{ix86} x86_64

%description

Processor Counter Monitor (PCM) is an application programming
interface (API) and a set of tools based on the API to monitor
performance and energy metrics of Intel(r) Core(tm), Xeon(r), Atom(tm)
and Xeon Phi(tm) processors. PCM works on Linux, Windows, Mac OS X,
FreeBSD and DragonFlyBSD operating systems.

%prep
%autosetup

%build
%set_build_flags
%make_build

%install
%make_install

%files
%license license.txt
%doc LINUX_HOWTO.txt README.md FAQ.md
%{_sbindir}/%{name}-core
%{_sbindir}/%{name}-iio
%{_sbindir}/%{name}-latency
%{_sbindir}/%{name}-lspci
%{_sbindir}/%{name}-memory
%{_sbindir}/%{name}-msr
%{_sbindir}/%{name}-numa
%{_sbindir}/%{name}-pcicfg
%{_sbindir}/%{name}-pcie
%{_sbindir}/%{name}-power
%{_sbindir}/%{name}-sensor
%{_sbindir}/%{name}-sensor-server
%{_sbindir}/%{name}-tsx
%{_sbindir}/%{name}-raw
%{_sbindir}/%{name}
%{_bindir}/%{name}-client
%{_sbindir}/%{name}-daemon
%{_sbindir}/%{name}-bw-histogram
%{_datadir}/%{name}/

%changelog
* Tue Apr 13 2021 Roman Dementiev <roman.dementiev@intel.com> 0.1-7
- Implement suggestions from Fedora review.

* Fri Mar 26 2021 William Cohen <wcohen@redhat.com> 0.1-6
- Clean up pcm.spec.

* Tue Aug 25 2020 Roman Dementiev <roman.dementiev@intel.com> 0.1-5
- Add pcm-raw under %files

* Wed Apr 01 2020 Otto Bruggeman <otto.g.bruggeman@intel.com> 0.1-4
- Add pcm-sensor-server under %files

* Mon Nov 25 2019 Roman Dementiev <roman.dementiev@intel.com> 0.1-3
- call make install and use %{_sbindir} or %{_bindir}

* Mon Oct 21 2019 Roman Dementiev <roman.dementiev@intel.com> 0.1-2
- add opCode file to /usr/share/pcm
- use "install" to copy pcm-bw-histogram.sh

* Fri Oct 18 2019 Roman Dementiev <roman.dementiev@intel.com> 0.1-1
- created spec file


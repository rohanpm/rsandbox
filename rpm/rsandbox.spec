Name:           rsandbox
Version:        git
Release:        0.80
License:        MIT
Summary:        Lightweight sandbox utility
Url:            https://github.com/rohanpm/rsandbox
Group:          Productivity/Other
Source:         rsandbox-%{version}.tar.gz
BuildRequires:  asciidoc
BuildRequires:  gcc-c++
BuildRequires:  pkg-config
BuildRequires:  pkgconfig(fuse)

%if 0%{?fedora} || 0%{?rhel}
BuildRequires:  libxslt
Requires(post): libcap
%define setcap  /sbin/setcap
%endif

%if 0%{?suse_version}
BuildRequires:  xsltproc
Requires(post): libcap-progs
%define setcap  /sbin/setcap
%endif

%if 0%{?mandriva_version}
BuildRequires:  xsltproc
Requires(post): libcap-utils
%define setcap  /usr/sbin/setcap
%endif

BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
Utility for sandboxing programs.
.
May be used to prevent a program from accessing network, filesystem,
IPC or other resources.

%prep
%setup -q

%build
make %{?_smp_mflags}

%install
%make_install prefix=%{_prefix}

%post
%setcap cap_sys_admin,cap_sys_chroot+pe %{_bindir}/rsandbox

%files
%defattr(-,root,root)
%{_bindir}/rsandbox
%{_mandir}/man1/rsandbox.1.gz

%changelog


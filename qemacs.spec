%define name qemacs
%define version 0.3pre3
%define release 1

Summary: qemacs (qe) - a small editor with some special features
Name: %{name}
Version: %{version}
Release: %{release}
Source0: http://fabrice.bellard.free.fr/qemacs/%{name}-%{version}.tar.gz
#Patch0: qemacs-0.2-makefile.patch
Copyright: LGPL
Group: Editors
BuildRoot: %{_tmppath}/%{name}-buildroot
Prefix: %{_prefix}
URL: http://fabrice.bellard.free.fr/qemacs/

# for the character maps:
#BuildRequires: yudit
Provides: qe

%description
QEmacs is an terminal-based text editor (with Emacs look&feel) with a small 
size and some unique features:

* Full screen editor with an Emacs compatible key subset (including undo and 
  incremental search) and emacs look and feel. 
* Can edit huge files (e.g. 100MBytes) without being slow by using a highly 
  optimized internal representation and by mmaping the file. 
* Full UTF8 support, including double width chars such as ideograms, provided 
  you have an UTF8 VT100 emulator such as a recent xterm. 
* Bidirectional editing respecting the Unicode Bidir algorithm (for Hebrew or 
  Arabic). 
* Can optionnaly contain input methods from the Yudit editor for most 
  languages, including Chinese CJ, Hebrew and Arabic. 
* Hexadecimal editing mode with insertion and block commands. Can edit binary 
  files as well as text files. 

%prep
%setup -n %{name}
%patch0 -p0 -b .tzafrir

%build
#./configure --prefix=%{prefix}
make PREFIX=%{prefix} CFLAGS="$RPM_OPT_FLAGS" 

%install
install -d $RPM_BUILD_ROOT%{prefix}/bin
make prefix=$RPM_BUILD_ROOT%{prefix} install 

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc CHANGES COPYING README TODO
%{_prefix}/bin/qe
%{_prefix}/share/qe/kmaps
%{_prefix}/share/qe/ligatures
%{_prefix}/man/man1/qe.1

%changelog
* Wed May 30 2001 Tzafrir Cohen <tzafrir@technion.ac.il> 0.2-1
- initial spec file. Added patch that also includes all of yudit's maps

# end of file

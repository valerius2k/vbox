VirtualBox Guest Additions for OS/2
===================================

This is the alternative version of OS/2 Additions for VirtualBox
(maintained by Valery V. Sedletski). It differs from Knut's additions,
distributed by Oracle, is some aspects.

Our implementation of the shared folders IFS is completely different
from Knut/Oracle version. Our version has in common with Knut's one
mostly in the assembler thunk code. We took an old Knut's version as a
base. The impression we had that Knut's version was abandoned. That time,
the IFS entry points in Knut's version were just stubs. After I (Valery)
said to Knut, that my version is almost working, he almost immediately
uploaded a newer version of his sources to Oracle svn. So, now we have
two different implementations of OS/2 additions. As we already did the
work done, we decided to continue developing it. Some features in our
version are unique, so we don't want to abandon it in favor of Knut's
version.

To distinguish between two different versions of the IFS, we decided
to rename our version to VBoxFS, whereas, Knut's version is called
VBoxSF.

The full source code for our version of OS/2 additions is available
at Netlabs svn: http://svn.netlabs.org/repos/vbox/trunk, together
with full sources for our VirtualBox OS/2 port.

Installation instructions:
-------------------------

The OS/2 additions are distributed as a zipped ISO image. You need to
attach the ISO as a CD image in VBox and follow the instructions below.

First, you should copy the files to appropriate places, namely

- Copy DLL's in usr\lib\* to %unixroot%\usr\lib
- Copy os2\dll\vbxgradd.dll to bootdrv:\os2\dll
- Copy drivers to bootdrv:\os2\boot
- Copy os2\*.exe to bootdrv:\os2

Where "bootdrv" is the OS/2 boot drive, and %unixroot% is where your UNIX
ports tree is installed (look into your config.sys for "set unixroot=..."
statement). In case you haven't installed the UNIX ports tree, you'll
need to create the "/usr/lib" subdirectory on some of your drives and
add this drive to "set unixroot=d:" env. variable in your "config.sys".

(you could just copy the files to your boot drive as they are on the
Additions ISO, preserving paths).

If you already have UNIX ports installed in your system (like in latest
eCS 2.2 or ArcaOS systems), then no need to copy usr\lib\*.dll over.
You just need to update your kLIBC to the latest version (which is
called libcn0.dll, "libc, next generation"), plus you need to install
libcx from yum repo (packages are named "libc" and "libcx"). It's
possible that you'll need to enable the "netlabs-exp" experimental
yum repo, to install libcn.

Add the following lines to config.sys:

! [============ config.sys ==============]
! device=d:\os2\boot\VBoxGuest.sys
! ifs=d:\os2\boot\VBoxFS.ifs
! run=d:\os2\VBoxService.exe
! ...
! rem device=d:\os2\boot\mouse.sys
! device=d:\os2\boot\VBoxMouse.sys
! ...
! set gradd_chains=c1
! rem set c1=vbe2grad
! rem set greext=panogrex
! set c1=vbxgradd
! [============ config.sys ==============]

Notes:

a) vbxgradd.dll is a renamed version of GenGRADD. This version should be used instead
of any other GRADD driver. So, the prerequisite is an installed GRADD driver (GenGRADD
or Panorama, or SNAP, etc.). You only need to change your GRADD module to VBXGRADD (see
our config.sys example above -- just need to comment an older "SET C1=..." line and add
a new one with VBXGRADD). Also, you need to REM out your "SET GREEXT=..." line, if it's
present.

b) VBoxMouse.sys is a modified VBox-aware version of MOUSE.SYS. So, you should
comment the original MOUSE.SYS out, and add a device=... line for VBoxMouse.sys.

c) Note that VBoxService.exe is a VBox additions daemon, which is a PM application,
because it contains a clipboard sharing code. So, it should be added as a run=...
to config.sys. Because it is a PM application, OS/2 kernel will postpone loading
VBoxService.exe until PM is started. So, no need to call it from startup.cmd, as it
was done in older versions.

Though, we encountered a problem when adding VBoxService.exe to config.sys: llaecs.exe
when started from mptstart.cmd, hangs. It also hangs on boot in some other cases.
Yes, llaecs.exe often causes problems on boot. (llaecs.exe is required to support
windoze "link-local addresses", those starting from 169.x.x.x, which are even not
standards-compliant). It was never worked for me, so I always comment it out from
"d:\mptn\bin\mptstart.cmd". So, you could do the same as well. Or, alternatively,
you can start VBoxService.exe from startup.cmd instead.

Note: llaecs.exe is present in some versions of eComStation only. It is removed in
ArcaOS, so it does not cause problems for ArcaOS users.

Drive letter pinning and automounting of shared folders.
-------------------------------------------------------

If you define some shared folders in VBox GUI, and set an "Auto mount" checkbox,
these shared folders will be automatically mounted at the first available drive
letters. This is done by VBoxService.exe, when it is starting up.

Also, there's an option to manually mount/unmount your shared folders. For that,
there's the VBoxFSAttach.exe utility. The command line syntax is the following:

! VBoxFSAttach <drive letter> <shared folder name>

to attach, or

! VBoxFSAttach <drive letter> /delete

to detach it.

Sometimes, first available drive letters are not convenient. For example, I need a drive
letter for my OS/2 data partition (JFS file system) to be F: in both OS/2, running on a
real hardware, and the OS/2 VM in VBox under Linux. For such cases, we added a drive
letter pinning feature. Just create the vboxfs.cfg file in bootdrv:\os2\boot with the
following contents (one shared folder per line):

! [================= vboxfs.cfg =====================]
! os2f		f:
! os2l		l:
! valerius	m:
! [================= vboxfs.cfg =====================]

The template is supplied with this Additions CD in the "os2\boot\vboxfs.cfg-sample" file. So,
the format is simple: each line specifies a drive letter mapping: a drive letter is mapped
to a shared folder name. Comments are allowed. Each comment starts at ';' or '#' symbol,
until the end of line. If drive letter, specified in vboxfs.cfg, was occupied by another
file system, then a first available drive letter will be used.

UNC path names.
--------------

The IFS supports the UNC path names. It serves UNC names starting with \\vboxsvr, \\vboxsrv,
or \\vboxfs. You can use these three UNC server names equally (any of the three will work).
So, for a log.txt in the "logs" subdirectory of the "valerius" shared folder, you can use a
\\vboxsrv\valerius\logs\log.txt UNC path name. Such UNC pathnames can be used instead of
using drive letters. So. \\vboxsrv\valerius\logs\log.txt is equivalent to m:\logs\log.txt
in our example.

The UNC pathname of a current shared folder is returned by DosQueryFSAttach API, specified
with FSAIL_QUERYNAME parameter. This feature is used by File Commander/2, for example. The
drive selection dialog displays the UNC pathname of a current shared folder instead of a
volume label. The same feature shows a resource name for network resources of resources
mounted with NetDrive.

VBoxFS.ifs command line switches.
--------------------------------

VBoxFS.ifs supports the following command line switches:

! /Q         Quiet initialization. Do not output any messages.
! /V         Verbose initialization. Output some diagnostic messages on init.
! /D         Output debug messages. The messages are sent to VBox log (which is limited to
!            32768 messages, after which messages from VBoxFS.ifs are muted by VBox). Also,
!            messages are sent in parallel to QSINIT / ARCALDR / OS4LDR log buffer. These
!            three loaders have common logging code, but are configured a bit differently.
!            If QSINIT / ARCALDR / OS4LDR are not available on your system, messages are
!            sent directly to a COM port. See "debug.txt" document for more info on debugging
!            problems in different QSINIT / ARCALDR / OS4LDR enabled drivers.

Credits:
-------

- Knut Stange Osmundsen (aka bird) for initial VBox port and OS/2 Additions
- Dmitry Kuminov (aka Dmik) for Qt and support in making VirtualBox 5.x port to OS/2
- Lars Erdmann, for code for generating empty EA's list (ported from fat32.ifs)
- Alexey Timoshenko (AlexT) and Vyacheslav Gnatenko (moveton), for support with 
OS/4-related problems (hardware-assisted virtualization support in VBox and more)
- Dmitry Zavalskov (_dixie_) for help with QSINIT, ARCALDR and related problems
(debugging and more).

The current version.
-------------------

The link to current version is available from Netlabs' Trac page for our OS/2 VirtualBox
port:

! http://trac.netlabs.org/vbox/

Also, there is a builds archive at:

! ftp://osfree.org/upload/vbox/additions/

The port is maintained by
BWW Bitwise works GmbH and
Valery V. Sedletski
(c) 2015-2019

VirtualBox Guest Additions Driver for OS/2
==========================================

This is the alternative version of OS/2 Additions for VirtualBox
(maintained by Valery V. Sedletski). It differs from Knut's additions,
distributed by Oracle, is some aspects.

Our implementation is completely different from Knut/Oracle version.
Our port has in common with Knut's only in the assembler thunk code.
(We took an old Knut's version as a base. That time, IFS entry points
in Knut's version were just stubs). After I said to Knut, that my
version is almost working, he uploaded a newer version of his sources
to Oracle svn. So, now we have two different implementations of OS/2
additions.

The full source code for our version of OS/2 additions is available
at Netlabs svn: http://svn.netlabs.org/repos/vbox/trunk, together
with full sources for our VirtualBox OS/2 port.

Installation instructions:
-------------------------

First, you should copy the files to appropriate places, namely

- Copy DLL's in usr\lib\* to %unixroot%\usr\lib
- Copy os2\dll\vbxgradd.dll to bootdrv:\os2\dll
- Copy drivers to bootdrv:\os2\boot
- Copy os2\*.exe to bootdrv:\os2

(you could just unpack the files to your boot drive as they are on the
Additions ISO).

Add the following lines to config.sys:

[============ config.sys ==============]
device=d:\os2\boot\VboxGuest.sys
ifs=d:\os2\boot\VBoxFS.ifs
run=d:\os2\VBoxService.exe
...
rem device=d:\os2\boot\mouse.sys
device=d:\os2\boot\VBoxMouse.sys
...
set gradd_chains=c1
rem set c1=vbe2grad
rem set greext=panogrex
set c1=vbxgradd
[============ config.sys ==============]

Notes:

a) vbxgradd.dll is a renamed version of GenGRADD. This version should be used instead
of any other GRADD driver. So, the prerequisite is an installed GRADD driver (GenGRADD
or Panorama, or SNAP). You only need to change your GRADD module to VBXGRADD (see our
config.sys example above -- just need to comment an older "SET C1=..." line and add a
new one with VBXGRADD.DLL).

b) VBoxMouse.sys is a modified version of MOUSE.SYS. So, you should comment the original
MOUSE.SYS out, and add a device=... line for VBoxMouse.sys.

c) Note that VBoxService.exe is a VBox additions daemon, which is a PM application,
because it contains a clipboard sharing code. So, it should be added as a run=...
to config.sys. Because it is a PM application, OS/2 kernel will postpone loading
VBoxService.exe until PM is started. So, no need to call it from startup.cmd, as it
was done in older versions. Though, we encountered a problem when adding VBoxService.exe
to config.sys: llaecs.exe when started from mptstart.cmd, hangs. It also hangs on
boot in some other cases. Yes, llaecs.exe often causes problems on boot. (llaecs.exe
is required to support windoze "link-local addresses", those starting from 169.x.x.x,
which are even not standards-compliant). It was never worked for me, so I always comment
it out from d:\mptn\bin\mptstart.cmd. So, you could do the same as well. Or, alternatively,
you can start VBoxService.exe from startup.cmd instead.

Drive letter pinning and automounting of shared folders.
-------------------------------------------------------

If you define some shared folders in VBox GUI, and set an "Auto mount" checkbox,
these shared folders will be automatically mounted at the first available drive letters.
This is done when VBoxService.exe is starting.

Sometimes, first available drive letters are not convenient. For example, I need a drive
letter for my OS/2 data partition (JFS file system) to be F: in both native OS/2 and the
OS/2 VM in VBox under Linux. For such cases, we added a drive letter pinning feature. Just
create the vboxfs.cfg file in bootdrv:\os2\boot with the following contents:

[================= vboxfs.cfg =====================]
os2f		f:
os2l		l:
valerius	m:
[================= vboxfs.cfg =====================]

The template is supplied with this Additions CD in the os2\boot\vboxfs.cfg-sample file. So,
the format is simple: each line specifies a drive letter mapping: a drive letter is mapped
to a shared folder name. Comments are allowed. Each comment starts at ';' or '#' symbol.
If drive letter, specified in vboxfs.cfg, was occupied by another file system, then a first
available drive letter will be used.

UNC path names.
--------------

The IFS supports the UNC path names. It serves UNC names starting with \\vboxsvr, \\vboxsrv,
or \\vboxfs. So, for a log.txt in the root directory of the "valerius" shared folder, you
can use a \\vboxsrv\valerius\log.txt UNC path name. Such UNC pathnames can be used instead
of using drive letters. So. \\vboxsrv\valerius\log.txt is equivalent to m:\valerius\log.txt
in our example.

The UNC pathname of a current shared folder is returned by DosQueryFSAttach API, specified
with FSAIL_QUERYNAME parameter. This feature is used by File Commander/2, for example. The
Drive selection dialog displays the UNC pathname of a current shared folder instead of a
volume label. (The same feature shows a resource name for network resources of resources
mounted with NetDrive).

The current version.
-------------------

The link to current version is available from Netlabs' Trac page for our OS/2 VirtualBox
port:

http://trac.netlabs.org/vbox/

Also, there is a builds archive at:

ftp://osfree.org/upload/vbox/additions/

The port is maintained by
BWW Bitwise works GmbH and
Valery V. Sedletski
(c) 2015-2019

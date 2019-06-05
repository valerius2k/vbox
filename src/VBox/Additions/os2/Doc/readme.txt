VirtualBox Guest Additions Driver for OS/2
==========================================

This is the alternative version of OS/2 Additions for VirtualBox
(maintained by Valery V. Sedletski). It differs from Knut's additions,
distributed by Oracle, is some aspects.

Our implementation is completely different from Knut/Oracle version.
Our port has in common with Knut's mostly in the assembler thunk code.
We took an old Knut's version as a base. The impression we had that
Knut's version was abandoned. That time, the IFS entry points in Knut's
version were just stubs. After I said to Knut, that my version is
almost working, he immediately uploaded a newer version of his sources
to Oracle svn. So, now we have two different implementations of OS/2
additions. As we already did the work done, we decided to continue
developing it. Some features in our version are unique, so we don't
want to abandon it in favor of Knut's version.

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

(you could just copy the files to your boot drive as they are on the
Additions ISO, preserving paths).

Add the following lines to config.sys:

! [============ config.sys ==============]
! device=d:\os2\boot\VboxGuest.sys
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
a new one with VBXGRADD).

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

Note: llaecs.exe is present in some versions of eComStation only. It is removed in
ArcaOS, so it does not cause problems for ArcaOS users.

Drive letter pinning and automounting of shared folders.
-------------------------------------------------------

If you define some shared folders in VBox GUI, and set an "Auto mount" checkbox,
these shared folders will be automatically mounted at the first available drive letters.
This is done by VBoxService.exe, when it is starting up.

Also, there's an option to manually mount/unmount your shared folders. For that, there's
the VBoxFSAttach.exe utility. The command line syntax is the following:

! VBoxFSAttach <drive letter> <shared folder name>

to attach, or

! VBoxFSAttach <drive letter> /delete

to detach it.

Sometimes, first available drive letters are not convenient. For example, I need a drive
letter for my OS/2 data partition (JFS file system) to be F: in both native OS/2 and the
OS/2 VM in VBox under Linux. For such cases, we added a drive letter pinning feature. Just
create the vboxfs.cfg file in bootdrv:\os2\boot with the following contents:

! [================= vboxfs.cfg =====================]
! os2f		f:
! os2l		l:
! valerius	m:
! [================= vboxfs.cfg =====================]

The template is supplied with this Additions CD in the "os2\boot\vboxfs.cfg-sample" file. So,
the format is simple: each line specifies a drive letter mapping: a drive letter is mapped
to a shared folder name. Comments are allowed. Each comment starts at ';' or '#' symbol.
If drive letter, specified in vboxfs.cfg, was occupied by another file system, then a first
available drive letter will be used.

UNC path names.
--------------

The IFS supports the UNC path names. It serves UNC names starting with \\vboxsvr, \\vboxsrv,
or \\vboxfs. You can use these three UNC server names equally (any of the three will work).
So, for a log.txt in the root directory of the "valerius" shared folder, you can use a
\\vboxsrv\valerius\log.txt UNC path name. Such UNC pathnames can be used instead of using
drive letters. So. \\vboxsrv\valerius\log.txt is equivalent to m:\valerius\log.txt in
our example.

The UNC pathname of a current shared folder is returned by DosQueryFSAttach API, specified
with FSAIL_QUERYNAME parameter. This feature is used by File Commander/2, for example. The
Drive selection dialog displays the UNC pathname of a current shared folder instead of a
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
!            sent directly to a COM port.

Debugging mode (/D switch).
--------------------------

When Debug mode (/D on the command line) is enabled, messages are output to:

1) VBox log file

The VBox log file for a current VM is located in %HOME%\VirtualBox VMs\<VM_name>\Logs\VBox.log
by default. But it is possible to set VBox VM's directory (%HOME%\VirtualBox VMs) to some other
location in VBox settings. Also, if your host system is Windows, then c:\Users\<Your_user> or
c:\Documents and Settings\<Your_user> is used, instead of %HOME%.

So, the debug messages are output there. However, when more than 32768 messages are sent
by the VM, VBox considers that VM is "flooding", and mutes it's output. When doing complex
debugging with a lot of messages, this is inconvenient. So, we added the output to other
places as well.

2) QSINIT / ARCALDR / OS4LDR log buffer

If one of these three bootloaders is installed in your system, you can output to their log
buffer. The log buffer can be as large as several megabytes. Together with VBoxFS.ifs messages,
you can see the sequence of events from other drivers and the kernel. So, you can synchronize
events from VBoxFS.ifs to events in other places of the system.

If your system is booted successfully, you can get the contents of the log buffer as
simple as issuing a single command.

a) If you have an OS/4 kernel installed, the contents of a log buffer can be got by issuing
a command:

! copy kernlog$ log.txt

-- this reads the buffer from special \dev\kernlog$ driver (built into OS/4 kernel) to
log.txt file

b) If you have IBM's kernel, and a QSINIT bootloader installed, (or you have ArcaOS, so you
are using ARCALDR bootloader, which is a custom, stripped-down version of QSINIT) then issue
a command:

! copy oemhlp$ log.txt

-- this uses \dev\oemhlp$ driver (built into OS2LDR) instead

c) If you have QSINIT or ARCALDR (or a very old version of OS/4 kernel) and you have
ACPI.PSD driver installed, then issue a command:

! copy ___hlp$ log.txt

-- ACPI.PSD installs its own version of \dev\oemhlp$ driver, and renames QSINIT's version
of oemhlp$ to \dev\___hlp$. So, you need to use a renamed version instead.

3) Debugging using a terminal, attached to a COM port

When you have QSINIT / ARCALDR / OS4LDR installed, the usual way of getting the log is
copying the log buffer to a file with 'copy' command. This, however, requires the system
to be booted successfully, until a desktop. If your system traps, or hangs, or stops
booting, you are still able to get the logs. For that, you'll need another machine with
a terminal emulator program installed, plus a NULL-modem cable, attached to your debugee
machine's COM port. And yes, you'll need your debugee machine to have a COM port available.

Many modern machines, especially laptops, have no COM port available. In such cases, you
can use a COM port on a docking station, if available. Or, a PCMCIA/Express card COM port,
or, if your machine supports Intel AMT management tecnology, you can use AMT SOL (serial
over LAN) COM port via network. Machines with Intel AMT support include all modern
ThinkPads. Also, many newer Intel Core i5/i7 machines have support for Intel ME (which
includes AMT, as an option.

If you have a Linux machine available, you can use the "amtterm" command (the package
name is usually the same, so in e.g., Debian system, you need "apt-get install amtterm"
to install the package). So, you can connect to your AMT SOL COM port via network using
a LAN cable (usually, a wired LAN only).

AMT SOL COM port, or PCMCIA/Express card COM ports are seen as a standard PCI COM port
in the "pci.exe" utility output. So, you could note an I/O address there, and use it in
"dbport" QSINIT / ARCALDR / OS4LDR command, instead of the default I/O address, which is
0x3F8 (see below). Note that this does not require a PCMCIA or Express card stack to be
installed in your OS! Your bootloader sends messages directly to an I/O port, without
using any driver.

Enabling QSINIT / OS4LDR / ARCALDR to output messages.
-----------------------------------------------------

In QSINIT / OS4LDR case, you need to create the "os2ldr.ini" file in the root directory
of your OS/2 boot disk. As a minimum, it should contain such lines:

! [================= os2ldr.ini ===================]
! [config]
! default=1    ; default menu entry number to boot
! timeout=30   ; boot menu timeout: 30 sec
! dbport=0x3F8 ; COM port base address (0x3F8 for COM1)
! dbflags=0x11 ; debug flags (leave as is for a hardware COM port)
! ; dbflags=0  ; if it's 0, then log is output to screen
! logsize=2048 ; log size in kilobytes
! 
! [kernel]
! os4krnl = OS/4 kernel,VALIMIT=3072,PRELOAD=1,CTRLC=1
! os2ldr.arc = ARCALDR, restart
! [================= os2ldr.ini ===================]

-- the relevant options here are "dbport", which specifies the COM port I/O base (0x3F8
for COM1, use a value from "pci.exe" output for AMT SOL, or PCMCIA/Express card, or some
other PCI com port), and "logsize", which specifies the log size in kilobytes. If it's
zero, the log buffer is disabled. So, if the .ini file is missing, or these options are
omitted, logging is disabled.

The "config" section sets global options. The "kernel" section specifies the list of
menu entries (the list of kernels to boot). Each kernel has it's local options (local
for that kernel). Most options from "config" section can be set locally for a particular
kernel. Local options are delimited by commas. Except for kernels, QSINIT can also
boot another version of OS2LDR (specified with "restart" option). As you can see here,
ARCALDR (renamed to "os2ldr.arc") can be booted as a second menu entry. For the full
list of options, consult QSINIT documentation.

If you have ArcaOS installed, you can enable the log buffer too. For that, you'll
need to create the "os2ldr.cfg" file in the root directory. ARCALDR supports a subset
of QSINIT options. So, for ARCALDR to support logging, you'll need the following
minimal "os2ldr.cfg" file:

! [================= os2ldr.cfg ===================]
! dbport=0x3F8
! logsize=2048
! [================= os2ldr.cfg ===================]

As you can see here, the options are the same. The difference is the file name and that
it has no sections. It has a list of pairs "variable=value".

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

VirtualBox for OS/2 OSE Edition BETA 
====================================

Version 5.0.6_OSE r141

14.11.2016							

This is a build of VirtualBox 5.0.6_OSE_r141 Edition for OS/2.

bww bitwise works GmbH. undertook the effort to provide an updated 
version of VirtualBox for OS/2. Please note, that this version is not
supported by Oracle Corporation.

DO NOT CONTACT ORACLE Corp. REGARDING THE OS/2 VERSION OF VIRTUALBOX 
NO MATTER WHAT YOUR QUESTION IS ABOUT! THANK YOU FOR UNDERSTANDING.

How to "Install" and Run
------------------------

1. Run the following lines in order to install all required rpm/yum packages

   yum install libc libgcc1 libcx libstdc++6 libstdc++
   yum install libsupc++6 libsupc++ libgcc-fwd
   yum install gettext libxml2 libxslt openssl libcurl zlib 
   yum install libidl libvncserver libaio SDL glib2 
   yum install libqt4 pthread libvpx libpng libjpeg
   yum install urpo yum install expat curl mmap

2. Unpack this archive somewhere.

3. Make sure you have a dot (.) in your LIBPATH statement in CONFIG.SYS.

4. Put the following line at the beginning of your CONFIG.SYS
   and reboot:

     DEVICE=<somewhere>\VBoxDrv.sys

5. Go to <somewhere> and run VirtualBox.exe (Qt GUI frontend).

6. Note that by default VirtualBox stores all user data in the
   %HOME%\.VirtualBox directory. If %HOME% is not set, it will use
   the <boot_drive>:\.VirtualBox directory. In either case, you may
   overwrite the location of this directory using the VBOX_USER_HOME
   environment variable.

7. For best performance, it is recommended to install the VirtualBox
   Guest Additions to the guest OS. The archive containing the ISO
   image with Guest Additions for supported guest OSes (Windows,
   Linux, OS/2) is named

     VBoxGuestAdditions_5.0.6.iso

   where 5.0.6 is the version number (it's best if it matches the version
   number of this VirtualBox package).
   
   In case the ISO is missing from the archive it can be downloaded from
   http://download.virtualbox.org/virtualbox/5.0.6/
   

   Download this ZIP from the same location you took this archive from
   and unpack the contents to the directory containing VirtualBox.exe.
   After that, you can mount the Additions ISO in the Qt GUI by selecting
   Devices -> Install Guest Additions... from the menu.


Documentation and Support
-------------------------

Please visit http://www.virtualbox.org where you can find a lot of useful
information about VirtualBox. There is a Community section where you can
try to request some help from other OS/2 users of VirtualBox.

You can download the User Manual for the latest official release of
VirtualBox using this URL:

  http://www.virtualbox.org/download/UserManual.pdf


OS/2 Specific Features
----------------------

This section describes the features that are specific to the OS/2 version
of VirtualBox and may be absent in versions for other platforms.

1. System key combinations such as Alt+Tab, Ctrl+Esc are currently always
   grabbed by the host and never reach the guest even when the keyboard
   is captured. In order to send these combinations to the guest OS, use
   the following shortcuts (where Host is the host key defined in the
   global settings dialog):

   Host+` (Tilde/Backquote)  =>  Ctrl+Esc
   Host+1                    =>  Alt+Tab
   Host+2                    =>  Alt+Shift+Tab

2. If you use two or more keyboard layouts on the OS/2 host (e.g. English
   and Russian), make sure that the keyboard is switched to the English
   layer when you work in the VirtualBox VM console window. Otherwise, some
   shortcuts that involve the Host key (in particluar, all Host+<latin_letter>
   shortcuts like Host+Q) may not work. Please note that the guest keyboard
   layout has nothing to do with the host layout so you will still be able to
   switch layouts in the guest using its own means.

3. Make sure you do not do 'set LIBPATHSTRICT=T' in the environment you start
   VirtualBox from: it will make the VirtualBox keyboard hook screw up your
   host desktop (a workaround is to be found).
   
4. AMD-V/VTx is not supported on OS/2 host (and never was).Hardware-assisted 
   virtualization is not supported, because some features are missing from 
   OS/2 kernel.

5. The PAE/NX setting which is required by certain Linux distributions in
   order to work, is available and workable.
   
6. Best experience with RAM hungry guests such as Windows currently can be 
   achieved by using the unofficial OS/4 Kernel and setting VALIMIT to 3072.



Current Issues / TODOs
----------------------

* FE/Qt (Qt GUI frontend):

  - Seamless mode (no top-level window transparency on OS/2).
  - Keyboard driver to intercept system key combinations
    (Alt+Tab etc.)

* Devices:

  - Audio.
  - Host interface networking.
  - Internal networking.
  - USB proxying.

* Misc:

  - Shared clipboard.
  - Installer.
  - Very slow Resume after Pause in real mode guest applications.


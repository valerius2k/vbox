rem *** ================================================== ***
rem *** Mark VBox DLL's for loading to high mmeory         ***
rem *** Put this file in the directory with VBox binaries  ***
rem *** ================================================== ***

for %%i in (vboxxpc vboxsis vboxsfld vboxsclp vboxhlp vboxhchn vboxdnd ^
    vboxdbg vboxdbgd vboxcapi vboxauts vboxauth) do ^
  if exist %%i.dll highmem -b %%i.dll

for %%i in (vboxsvcm vboxipcc vboxc) do ^
  if exist components\%%i.dll ^
    highmem -b components\%%i.dll

for %%i in (vboxdtc vboxdtrc) do ^
  if exist ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\os2.x86\%%i.dll ^
     highmem -b ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\os2.x86\%%i.dll

for %%i in (vboxvnc vboxvncm) do ^
  if exist ExtensionPacks\VNC\os2.x86\%%i.dll ^
     highmem -b ExtensionPacks\VNC\os2.x86\%%i.dll

rem -- code only!
for %%i in (vboxdd vboxdd2 vboxddu vboxrem vboxrm32 vboxrm64 ^
    vboxrt vboxvmm vboxxcom) do ^
  if exist %%i.dll highmem -c %%i.dll

for %%i in (pthr01 png1616 mmap libxml2 libvpx2 jpeg expat7 curl7 ^
    crypto10 crypt32 aio) do ^
  if exist %%i.dll highmem -b %%i.dll

rem -- code only!
for %%i in (ssl10 stdcpp6 urpo z) do ^
  if exist %%i.dll highmem -c %%i.dll

rem *** ================================================== ***
rem *** Change extpacks name in XML files                  ***
rem *** Put this file in the directory with VBox binaries  ***
rem *** ================================================== ***

sed -e "s/VBoxDTraceMain/VBoxDTrc/g" ^
    < ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\ExtPack.xml ^
    > ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\ExtPack2.xml

del ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\ExtPack.xml

move ^
    ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\ExtPack2.xml ^
    ExtensionPacks\Oracle_VBoxDTrace_Extension_Pack\ExtPack.xml

sed -e "s/VBoxVNCMain/VBoxVNCM/g" ^
    < ExtensionPacks\VNC\ExtPack.xml ^
    > ExtensionPacks\VNC\ExtPack2.xml

del ExtensionPacks\VNC\ExtPack.xml

move ^
    ExtensionPacks\VNC\ExtPack2.xml ^
    ExtensionPacks\VNC\ExtPack.xml

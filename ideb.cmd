rem *** ================================================== ***
rem *** IDEBUG debugger wrapper                            ***
rem *** Put this file in the directory with VBox binaries  ***
rem *** ================================================== ***
@echo off
@rem VAC path
set vac=f:\dev\vac365
set oldpath=%path%
set olddpath=%dpath%
set path=%vac%\bin;%oldpath%
set beginlibpath=%vac%\dll
set dpath=%vac%\msg;%vac%\help;%vac%\runtime;%olddpath%
idebug %1 %2 %3 %4 %5 %6 %7 %8 %9
set path=%oldpath%
set dpath=%olddpath%

@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64 >nul
cd /d d:\project\HGS-IRP-main
cl /nologo /EHsc /std:c++14 /O2 *.cpp /Fe:irp.exe

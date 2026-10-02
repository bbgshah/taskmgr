@echo off
rem Run from an "x64 Native Tools Command Prompt for VS"
rc /nologo resource.rc || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE main.cpp resource.res /Fe:taskmgr.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO

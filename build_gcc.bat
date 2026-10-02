@echo off
rem Run from a MinGW-w64 / MSYS2 shell where g++ and windres are on PATH
windres resource.rc -O coff -o resource.o || exit /b 1
g++ -std=c++17 -O2 -s -DUNICODE -D_UNICODE main.cpp resource.o -o taskmgr.exe -municode -mwindows -static -lcomctl32 -ldwmapi -luxtheme -lpdh -liphlpapi -lpsapi -lgdiplus -lshell32 -ladvapi32 -lole32 -lws2_32 -lgdi32 -luser32

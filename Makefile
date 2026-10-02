# GCC / MinGW-w64 build:  make        (Linux cross: make CROSS=x86_64-w64-mingw32-)
CROSS ?=
CXX    = $(CROSS)g++
WINDRES= $(CROSS)windres
CXXFLAGS = -std=c++17 -O2 -s -DUNICODE -D_UNICODE
LDFLAGS  = -municode -mwindows -static
LIBS = -lcomctl32 -ldwmapi -luxtheme -lpdh -liphlpapi -lpsapi -lgdiplus -lshell32 -ladvapi32 -lole32 -lws2_32 -lgdi32 -luser32

taskmgr.exe: main.cpp resource.o
	$(CXX) $(CXXFLAGS) main.cpp resource.o -o $@ $(LDFLAGS) $(LIBS)
resource.o: resource.rc resource.h taskmgr.ico taskmgr.manifest
	$(WINDRES) resource.rc -O coff -o $@
clean:
	rm -f taskmgr.exe resource.o

# F4Menu Makefile
# Compatible with MinGW/GCC and MSVC (via nmake)

# Compiler detection
!IF "$(CC)" == ""
CC = gcc
!ENDIF

# Compiler-specific settings
!IF "$(CC)" == "cl" || "$(CC)" == "cl.exe"
# MSVC settings
CFLAGS = /nologo /W3 /O2 /D_UNICODE /DUNICODE
LDFLAGS = /link /SUBSYSTEM:WINDOWS
LIBS = user32.lib gdi32.lib shell32.lib comctl32.lib comdlg32.lib
OUTPUT = /Fe:F4Menu.exe
CLEAN_FILES = F4Menu.exe F4Menu.obj
!ELSE
# MinGW/GCC settings
CFLAGS = -O2 -Wall -municode -mwindows
LDFLAGS = 
LIBS = -lcomctl32 -lshell32 -luser32 -lgdi32 -lcomdlg32
OUTPUT = -o F4Menu.exe
CLEAN_FILES = F4Menu.exe F4Menu.o
!ENDIF

# Target
TARGET = F4Menu.exe
SOURCE = F4Menu.c
RCFILE = F4Menu.rc
RESOBJ = F4Menu.res

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(SOURCE) $(RESOBJ)
    @echo Building F4Menu...
!IF "$(CC)" == "cl" || "$(CC)" == "cl.exe"
    $(CC) $(CFLAGS) $(OUTPUT) $(SOURCE) $(RESOBJ) $(LDFLAGS) $(LIBS)
!ELSE
    $(CC) $(CFLAGS) $(OUTPUT) $(SOURCE) $(RESOBJ) $(LDFLAGS) $(LIBS)
!ENDIF
    @echo Build complete!

# Resource compilation
!IF "$(CC)" == "cl" || "$(CC)" == "cl.exe"
$(RESOBJ): $(RCFILE)
    rc /nologo /fo $(RESOBJ) $(RCFILE)
!ELSE
$(RESOBJ): $(RCFILE)
    windres -i $(RCFILE) -o $(RESOBJ)
!ENDIF

# Clean target
clean:
	@echo Cleaning build files...
!IF "$(CC)" == "cl" || "$(CC)" == "cl.exe"
    @if exist F4Menu.exe del /Q F4Menu.exe
    @if exist F4Menu.obj del /Q F4Menu.obj
    @if exist F4Menu.res del /Q F4Menu.res
!ELSE
    @rm -f $(CLEAN_FILES) F4Menu.res
!ENDIF
	@echo Clean complete!

# Help target
help:
	@echo F4Menu Makefile
	@echo.
	@echo Targets:
	@echo   all     - Build F4Menu.exe (default)
	@echo   clean   - Remove build files
	@echo   help    - Show this help message
	@echo.
	@echo Usage:
	@echo   For MinGW: make
	@echo   For MSVC:  nmake

.PHONY: all clean help

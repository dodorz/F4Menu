/*
 * F4Menu - Windows File Launcher and Configuration Tool
 * 
 * A native Win32 application written in pure C that provides:
 * 1. Configuration Mode: GUI to manage program associations
 * 2. Launch Mode: Context menu to open files with configured programs
 * 
 * Compatible with Windows XP to Windows 11 (32-bit and 64-bit)
 * No frameworks - pure Win32 API only
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501  // Windows XP compatibility
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winver.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "version.lib")

// Constants
#include "version.h"

#define MAX_PROGRAMS 100
#define MAX_PATH_LEN 1024
#define MAX_PARAM_LEN 512
#define MAX_TYPE_LEN 1024
#define MAX_NAME_LEN 256
#define MAX_EXTRA_LEN 1024     // keys of a program section this version does not know
#define MAX_COMMENT_LEN 1024   // comment lines kept inside a program section

// Launch mode of a program
#define MODE_SEPARATE 0
#define MODE_MERGE 1
#define MODE_INHERIT (-1)    // take the [General] default

// Window state requested on launch
#define WINDOW_NORMAL 0
#define WINDOW_MAX 1
#define WINDOW_MIN 2
#define WINDOW_INHERIT (-1)  // take the [General] default

#define IDC_LISTVIEW 1001
#define IDC_BTN_ADD 1002
#define IDC_BTN_EDIT 1003
#define IDC_BTN_DELETE 1004
#define IDC_BTN_SAVE 1005
#define IDC_BTN_ABOUT 1006
#define IDC_BTN_EXIT 1007
#define IDC_BTN_UP 1008
#define IDC_BTN_DOWN 1009

#define IDD_EDIT_NAME 2001
#define IDD_EDIT_PATH 2002
#define IDD_EDIT_PARAM 2003
#define IDD_EDIT_START 2004
#define IDD_EDIT_ICON_FILE 2005
#define IDD_EDIT_TYPE 2006
#define IDD_COMBO_MODE 2007
#define IDD_COMBO_WINDOW 2008
#define IDD_BTN_BROWSE_PATH 2009
#define IDD_BTN_BROWSE_ICON_FILE 2010
#define IDD_BTN_OK 2011
#define IDD_BTN_CANCEL 2012
#define IDD_ICON_LIST 2013
#define IDD_ICON_INDEX 2014

// Program configuration structure.
// A program is one section of the config file: the section header is its display
// name and the position of the section in the file is its position in the list.
// 'extra' and 'comments' carry over the parts of a section this version does not
// interpret, so hand-written notes and keys survive a rewrite.
typedef struct {
    WCHAR name[MAX_NAME_LEN];
    WCHAR path[MAX_PATH_LEN];
    WCHAR param[MAX_PARAM_LEN];
    WCHAR start[MAX_PATH_LEN];
    WCHAR icon[MAX_PATH_LEN];
    WCHAR type[MAX_TYPE_LEN];
    int mode;                 // MODE_SEPARATE, MODE_MERGE or MODE_INHERIT
    int window;               // WINDOW_NORMAL/MAX/MIN or WINDOW_INHERIT
    WCHAR extra[MAX_EXTRA_LEN];
    WCHAR comments[MAX_COMMENT_LEN];
} ProgramConfig;

// DPI scaling
int g_dpi = 96;
#define DPI_SCALE(val) MulDiv(val, g_dpi, 96)

// Global variables
HINSTANCE g_hInst;
WCHAR g_iniPath[MAX_PATH];
ProgramConfig g_programs[MAX_PROGRAMS];
int g_programCount = 0;

// [General] defaults, applied to every program that does not set its own
int g_defaultMode = MODE_SEPARATE;
int g_defaultWindow = WINDOW_NORMAL;
HWND g_hListView = NULL;
HIMAGELIST g_hImageList = NULL;
HFONT g_hFont = NULL;

// Image list index displayed by each ListView row (-1 = no image)
int g_rowImage[MAX_PROGRAMS];

// Pre-fill data for add dialog
WCHAR g_prefillPath[MAX_PATH_LEN] = {0};
WCHAR g_prefillName[MAX_NAME_LEN] = {0};
WCHAR g_prefillStart[MAX_PATH_LEN] = {0};
WCHAR g_prefillType[MAX_TYPE_LEN] = {0};
int g_editIndex = -1;
int g_selectedIconIndex = 0;

// ListView drag-and-drop reordering state
BOOL g_dragging = FALSE;
int g_dragIndex = -1;
int g_dragTarget = -1;
HIMAGELIST g_dragImageList = NULL;

// Settings
typedef struct {
    int winPosX;
    int winPosY;
    int winWidth;
    int winHeight;
    int columnWidths[8];
} Settings;

Settings g_settings = {0};

// Custom INI path from command line
WCHAR g_customIniPath[MAX_PATH] = {0};

// Runtime data file path (.dat)
WCHAR g_datPath[MAX_PATH] = {0};

// Function declarations
void GetIniFilePath(WCHAR* path, DWORD size);
void LoadSettings();
void SaveSettings();
void LoadPrograms();
void SavePrograms();
void InitListView(HWND hwnd);
void PopulateListView();
void AddProgramToListView(int index);
void UpdateListViewRow(int row);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditDialogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData);
void ShowEditDialog(HWND parent, int index);
void DeleteSelectedPrograms(HWND hwnd);
void ShowAboutDialog(HWND parent);
void LaunchMode(int argc, WCHAR** argv);
BOOL MatchExtension(const WCHAR* fileExt, const WCHAR* typeList);
void ExecuteProgram(ProgramConfig* prog, WCHAR** files, int fileCount);
HICON LoadIconFromPath(const WCHAR* iconPath);
void ExpandEnvStrings(const WCHAR* src, WCHAR* dst, DWORD dstSize);
BOOL GetExeProductName(const WCHAR* exePath, WCHAR* name, DWORD nameSize);
void AddProgramFromFilePath(HWND hwnd, const WCHAR* exePath);

// Get INI file path
void GetIniFilePath(WCHAR* path, DWORD size) {
    if (wcslen(g_customIniPath) > 0) {
        wcscpy_s(path, size, g_customIniPath);
        return;
    }
    GetModuleFileNameW(NULL, path, size);
    WCHAR* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }
    wcscat_s(path, size, L"F4Menu.ini");
}

// Get .dat file path (same location as INI)
void GetDatFilePath(WCHAR* path, DWORD size) {
    if (wcslen(g_customIniPath) > 0) {
        wcscpy_s(path, size, g_customIniPath);
        WCHAR* dot = wcsrchr(path, L'.');
        if (dot) wcscpy_s(dot, size - (dot - path), L".dat");
        else wcscat_s(path, size, L".dat");
        return;
    }
    GetModuleFileNameW(NULL, path, size);
    WCHAR* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }
    wcscat_s(path, size, L"F4Menu.dat");
}

// Expand environment variables
void ExpandEnvStrings(const WCHAR* src, WCHAR* dst, DWORD dstSize) {
    ExpandEnvironmentStringsW(src, dst, dstSize);
}

// INI file parser - single read, in-memory parse (replaces per-field GetPrivateProfile API calls)
#define INI_MAX_ENTRIES 4096
#define INI_MAX_SECTIONS 256

// Read a text file into a newly allocated wide-char string.
// Supported encodings: UTF-16LE (BOM), UTF-8 (BOM or bare) and, when the data is
// not valid UTF-8, the system ANSI code page (this is what the Win32 profile API
// writes when every character fits in the ANSI code page).
static WCHAR* ReadTextFileW(const WCHAR* path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return NULL;
    }

    BYTE* rawBytes = (BYTE*)HeapAlloc(GetProcessHeap(), 0, fileSize);
    if (!rawBytes) {
        CloseHandle(hFile);
        return NULL;
    }

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hFile, rawBytes, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    if (!ok || bytesRead == 0) {
        HeapFree(GetProcessHeap(), 0, rawBytes);
        return NULL;
    }

    const BYTE* src = rawBytes;
    UINT srcLen = bytesRead;
    WCHAR* buffer = NULL;
    int wideLen = 0;

    if (bytesRead >= 2 && rawBytes[0] == 0xFF && rawBytes[1] == 0xFE) {
        // UTF-16LE with BOM
        src += 2;
        srcLen -= 2;
        wideLen = (int)(srcLen / sizeof(WCHAR));
        buffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, ((size_t)wideLen + 1) * sizeof(WCHAR));
        if (buffer) {
            CopyMemory(buffer, src, (size_t)wideLen * sizeof(WCHAR));
            buffer[wideLen] = L'\0';
        }
        HeapFree(GetProcessHeap(), 0, rawBytes);
        return buffer;
    }

    if (bytesRead >= 3 && rawBytes[0] == 0xEF && rawBytes[1] == 0xBB && rawBytes[2] == 0xBF) {
        src += 3;
        srcLen -= 3;
    }

    // UTF-8 first; fall back to the ANSI code page for legacy files
    UINT codePage = CP_UTF8;
    wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)src, (int)srcLen, NULL, 0);
    if (wideLen <= 0) {
        codePage = CP_ACP;
        wideLen = MultiByteToWideChar(CP_ACP, 0, (const char*)src, (int)srcLen, NULL, 0);
    }

    if (wideLen > 0) {
        buffer = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, ((size_t)wideLen + 1) * sizeof(WCHAR));
        if (buffer) {
            MultiByteToWideChar(codePage, 0, (const char*)src, (int)srcLen, buffer, wideLen);
            buffer[wideLen] = L'\0';
        }
    }

    HeapFree(GetProcessHeap(), 0, rawBytes);
    return buffer;
}

// Write text to a file in a single operation (instead of one full-file rewrite per
// key, as the profile APIs do). Always UTF-8 with BOM: section names are user data
// and may contain characters the ANSI code page cannot represent.
static BOOL WriteTextFileW(const WCHAR* path, const WCHAR* text, int charCount) {
    if (charCount < 0) charCount = (int)wcslen(text);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text, charCount, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return FALSE;

    BYTE* bytes = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (size_t)utf8Len + 3);
    if (!bytes) return FALSE;

    bytes[0] = 0xEF;
    bytes[1] = 0xBB;
    bytes[2] = 0xBF;
    WideCharToMultiByte(CP_UTF8, 0, text, charCount, (char*)(bytes + 3), utf8Len, NULL, NULL);

    DWORD byteCount = (DWORD)utf8Len + 3;
    BOOL ok = FALSE;
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = WriteFile(hFile, bytes, byteCount, &written, NULL) && written == byteCount;
        CloseHandle(hFile);
    }

    HeapFree(GetProcessHeap(), 0, bytes);
    return ok;
}

// Growable wide-char text buffer used to build a whole file image in memory
typedef struct {
    WCHAR* data;
    int len;
    int cap;
} TextBuf;

static BOOL TextBufGrow(TextBuf* tb, int extra) {
    if (extra < 0) return FALSE;
    if (tb->len + extra + 1 <= tb->cap) return TRUE;

    int cap = tb->cap > 0 ? tb->cap : 4096;
    while (cap < tb->len + extra + 1) {
        if (cap > (1 << 26)) return FALSE;  // sanity limit (~256 MB of wide text)
        cap *= 2;
    }

    WCHAR* p;
    if (tb->data) {
        p = (WCHAR*)HeapReAlloc(GetProcessHeap(), 0, tb->data, (size_t)cap * sizeof(WCHAR));
    } else {
        p = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (size_t)cap * sizeof(WCHAR));
    }
    if (!p) return FALSE;

    tb->data = p;
    tb->cap = cap;
    return TRUE;
}

static BOOL TextBufAppendN(TextBuf* tb, const WCHAR* s, int count) {
    if (count <= 0) return TRUE;
    if (!TextBufGrow(tb, count)) return FALSE;

    CopyMemory(tb->data + tb->len, s, (size_t)count * sizeof(WCHAR));
    tb->len += count;
    tb->data[tb->len] = L'\0';
    return TRUE;
}

static BOOL TextBufAppend(TextBuf* tb, const WCHAR* s) {
    return TextBufAppendN(tb, s, (int)wcslen(s));
}

static void TextBufFree(TextBuf* tb) {
    if (tb->data) {
        HeapFree(GetProcessHeap(), 0, tb->data);
        tb->data = NULL;
    }
    tb->len = 0;
    tb->cap = 0;
}

// Append "key=value\r\n"
static BOOL TextBufAppendKey(TextBuf* tb, const WCHAR* key, const WCHAR* value) {
    if (!TextBufAppend(tb, key)) return FALSE;
    if (!TextBufAppend(tb, L"=")) return FALSE;
    if (!TextBufAppend(tb, value)) return FALSE;
    return TextBufAppend(tb, L"\r\n");
}

// One parsed line of an INI file. section/key/value point into IniFile::buffer and
// stay valid until IniFree. Comment lines are kept as entries with key == NULL so
// that comments living inside a program section survive a rewrite of the file.
typedef struct {
    const WCHAR* section;   // NULL while no section header has been seen yet
    const WCHAR* key;       // NULL for comment lines
    const WCHAR* value;
} IniEntry;

typedef struct {
    WCHAR* buffer;
    IniEntry* entries;
    int count;
} IniFile;

static void IniFree(IniFile* ini) {
    if (ini->buffer) {
        HeapFree(GetProcessHeap(), 0, ini->buffer);
        ini->buffer = NULL;
    }
    if (ini->entries) {
        HeapFree(GetProcessHeap(), 0, ini->entries);
        ini->entries = NULL;
    }
    ini->count = 0;
}

// Parse a text buffer the caller owns; IniFile takes over the buffer.
static void IniParseText(IniFile* ini, WCHAR* text) {
    ini->count = 0;
    ini->buffer = text;
    ini->entries = NULL;
    if (!text) return;

    ini->entries = (IniEntry*)HeapAlloc(GetProcessHeap(), 0, sizeof(IniEntry) * INI_MAX_ENTRIES);
    if (!ini->entries) return;

    const WCHAR* curSection = NULL;
    WCHAR* context = NULL;
    WCHAR* line = wcstok_s(text, L"\r\n", &context);

    while (line && ini->count < INI_MAX_ENTRIES) {
        WCHAR* p = line;
        while (*p == L' ' || *p == L'\t') p++;
        if (*p == L'\0') { line = wcstok_s(NULL, L"\r\n", &context); continue; }

        if (*p == L'[') {
            WCHAR* end = wcschr(p, L']');
            if (end) {
                *end = L'\0';
                curSection = p + 1;
                WCHAR* e = (WCHAR*)curSection + wcslen(curSection);
                while (e > (WCHAR*)curSection && (e[-1] == L' ' || e[-1] == L'\t')) *--e = L'\0';
            }
            line = wcstok_s(NULL, L"\r\n", &context);
            continue;
        }

        IniEntry* entry = &ini->entries[ini->count];
        entry->section = curSection;

        if (*p == L';' || *p == L'#') {
            entry->key = NULL;
            entry->value = p;
            ini->count++;
            line = wcstok_s(NULL, L"\r\n", &context);
            continue;
        }

        WCHAR* eq = wcschr(p, L'=');
        if (eq) {
            *eq = L'\0';
            WCHAR* key = p;
            WCHAR* value = eq + 1;

            size_t klen = wcslen(key);
            while (klen > 0 && (key[klen - 1] == L' ' || key[klen - 1] == L'\t'))
                key[--klen] = L'\0';

            entry->key = key;
            entry->value = value;
            ini->count++;
        }

        line = wcstok_s(NULL, L"\r\n", &context);
    }
}

static BOOL IniLoad(IniFile* ini, const WCHAR* path) {
    WCHAR* text = ReadTextFileW(path);
    if (!text) return FALSE;

    IniParseText(ini, text);
    return TRUE;
}

static BOOL SectionEquals(const WCHAR* a, int aLen, const WCHAR* b, int bLen) {
    return aLen == bLen && _wcsnicmp(a, b, aLen) == 0;
}

static const WCHAR* IniGetN(IniFile* ini, const WCHAR* section, int sectionLen, const WCHAR* key) {
    for (int i = 0; i < ini->count; i++) {
        const IniEntry* entry = &ini->entries[i];
        if (!entry->key || !entry->section) continue;
        if (!SectionEquals(entry->section, (int)wcslen(entry->section), section, sectionLen)) continue;
        if (_wcsicmp(entry->key, key) == 0) return entry->value;
    }
    return NULL;
}

static const WCHAR* IniGet(IniFile* ini, const WCHAR* section, const WCHAR* key) {
    return IniGetN(ini, section, (int)wcslen(section), key);
}

// Extract product name from PE version info
BOOL GetExeProductName(const WCHAR* exePath, WCHAR* name, DWORD nameSize) {
    DWORD verHandle = 0;
    DWORD verSize = GetFileVersionInfoSizeW(exePath, &verHandle);
    if (verSize == 0) return FALSE;
    
    BYTE* verData = (BYTE*)HeapAlloc(GetProcessHeap(), 0, verSize);
    if (!verData) return FALSE;
    
    if (!GetFileVersionInfoW(exePath, 0, verSize, verData)) {
        HeapFree(GetProcessHeap(), 0, verData);
        return FALSE;
    }
    
    UINT len = 0;
    WCHAR* prodName = NULL;
    // Try Chinese first, then English, then neutral
    if (VerQueryValueW(verData, L"\\StringFileInfo\\080404b0\\ProductName", (LPVOID*)&prodName, &len) && len > 0) {
        wcscpy_s(name, nameSize, prodName);
        HeapFree(GetProcessHeap(), 0, verData);
        return TRUE;
    }
    if (VerQueryValueW(verData, L"\\StringFileInfo\\040904b0\\ProductName", (LPVOID*)&prodName, &len) && len > 0) {
        wcscpy_s(name, nameSize, prodName);
        HeapFree(GetProcessHeap(), 0, verData);
        return TRUE;
    }
    if (VerQueryValueW(verData, L"\\StringFileInfo\\000004b0\\ProductName", (LPVOID*)&prodName, &len) && len > 0) {
        wcscpy_s(name, nameSize, prodName);
        HeapFree(GetProcessHeap(), 0, verData);
        return TRUE;
    }
    
    HeapFree(GetProcessHeap(), 0, verData);
    return FALSE;
}

// Load settings from .dat file
void LoadSettings() {
    GetDatFilePath(g_datPath, MAX_PATH);
    
    DWORD winPos = GetPrivateProfileIntW(L"Settings", L"WinPos", 15598003, g_datPath);
    DWORD winSize = GetPrivateProfileIntW(L"Settings", L"WinSize", 25428634, g_datPath);
    
    g_settings.winPosX = LOWORD(winPos);
    g_settings.winPosY = HIWORD(winPos);
    g_settings.winWidth = LOWORD(winSize);
    g_settings.winHeight = HIWORD(winSize);
    
    // Default window size if invalid.
    // Note: CreateWindowExW receives the outer window size, and WM_CLOSE saves
    // the size from GetWindowRect, so the thresholds below compare like with
    // like (outer size, not client area).
    if (g_settings.winWidth < DPI_SCALE(400)) g_settings.winWidth = DPI_SCALE(800);
    if (g_settings.winHeight < DPI_SCALE(300)) g_settings.winHeight = DPI_SCALE(600);
    
    // Load column widths. Configurations written by older versions still contain
    // the removed "Main" column, so a 9-value string is mapped without it.
    WCHAR columnStr[512] = {0};
    GetPrivateProfileStringW(L"Settings", L"Column", L"24,150,200,100,150,60,60,150,",
                             columnStr, 512, g_datPath);

    int widths[16] = {0};
    int n = 0;
    WCHAR* context = NULL;
    WCHAR* token = wcstok(columnStr, L",", &context);
    while (token && n < 16) {
        widths[n++] = _wtoi(token);
        token = wcstok(NULL, L",", &context);
    }

    const int legacyMainColumn = 7;
    for (int i = 0; i < 8; i++) {
        int src = (n > 8 && i >= legacyMainColumn) ? i + 1 : i;
        g_settings.columnWidths[i] = src < n ? widths[src] : 0;
    }

    // Default column widths
    int defaults[8] = {24, 150, 200, 100, 150, 60, 60, 150};
    for (int i = 0; i < 8; i++) {
        if (g_settings.columnWidths[i] <= 0) {
            g_settings.columnWidths[i] = defaults[i];
        }
    }
}

// Save settings to .dat file
void SaveSettings() {
    WCHAR buffer[64];
    
    DWORD winPos = MAKELONG(g_settings.winPosX, g_settings.winPosY);
    DWORD winSize = MAKELONG(g_settings.winWidth, g_settings.winHeight);
    
    swprintf(buffer, 64, L"%u", winPos);
    WritePrivateProfileStringW(L"Settings", L"WinPos", buffer, g_datPath);
    
    swprintf(buffer, 64, L"%u", winSize);
    WritePrivateProfileStringW(L"Settings", L"WinSize", buffer, g_datPath);
    
    // Save column widths
    WCHAR columnStr[512] = {0};
    WCHAR temp[32];
    for (int i = 0; i < 8; i++) {
        int width = (int)SendMessageW(g_hListView, LVM_GETCOLUMNWIDTH, i, 0);
        if (width > 0) {
            swprintf(temp, 32, L"%d,", width);
            wcscat_s(columnStr, 512, temp);
        }
    }
    WritePrivateProfileStringW(L"Settings", L"Column", columnStr, g_datPath);
}

// ---------------------------------------------------------------------------
// Program configuration file format
//
// One section per program, in list order: the section header is the display
// name, so adding an entry means adding a section and removing one means
// removing a section - there is no counter to keep in sync. A section is a
// program if and only if it carries a non-empty Path key. [General] is managed
// too and holds the defaults; every other section, comment and blank line in the
// file is preserved verbatim.
// ---------------------------------------------------------------------------

static int ParseModeValue(const WCHAR* value) {
    if (!value || !*value) return MODE_SEPARATE;
    if (_wcsicmp(value, L"merge") == 0 || _wcsicmp(value, L"1") == 0) return MODE_MERGE;
    return MODE_SEPARATE;   // "separate", legacy 0, or anything unrecognized
}

static int ParseWindowValue(const WCHAR* value) {
    if (!value || !*value) return WINDOW_NORMAL;
    if (_wcsicmp(value, L"max") == 0 || _wcsicmp(value, L"maximized") == 0 || _wcsicmp(value, L"1") == 0)
        return WINDOW_MAX;
    if (_wcsicmp(value, L"min") == 0 || _wcsicmp(value, L"minimized") == 0 || _wcsicmp(value, L"2") == 0)
        return WINDOW_MIN;
    return WINDOW_NORMAL;   // "normal", legacy 0, or anything unrecognized
}

static const WCHAR* ModeName(int mode) {
    return (mode == MODE_MERGE) ? L"merge" : L"separate";
}

static const WCHAR* WindowName(int window) {
    if (window == WINDOW_MAX) return L"max";
    if (window == WINDOW_MIN) return L"min";
    return L"normal";
}

// Mode and Window are left out of the file when they are the default, so a
// program only carries the values it actually differs on. Changing a default in
// [General] therefore reaches every program that did not choose its own value.
static int ProgramMode(const ProgramConfig* prog) {
    return (prog->mode == MODE_INHERIT) ? g_defaultMode : prog->mode;
}

static int ProgramWindow(const ProgramConfig* prog) {
    return (prog->window == WINDOW_INHERIT) ? g_defaultWindow : prog->window;
}

// A program name is also a section header, so the characters that would end the
// header or split the line are dropped and the rest is trimmed.
static void SanitizeProgramName(WCHAR* name) {
    WCHAR* w = name;
    WCHAR* r = name;
    while (*w) {
        if (*w != L'[' && *w != L']' && *w != L'\r' && *w != L'\n') *r++ = *w;
        w++;
    }
    *r = L'\0';

    WCHAR* first = name;
    while (*first == L' ' || *first == L'\t') first++;
    if (first != name) {
        size_t len = wcslen(first);
        MoveMemory(name, first, (len + 1) * sizeof(WCHAR));
    }

    size_t len = wcslen(name);
    while (len > 0 && (name[len - 1] == L' ' || name[len - 1] == L'\t')) name[--len] = L'\0';
}

// Fallback display name for a program whose section header is blank
static void ProgramNameFromPath(const WCHAR* path, WCHAR* out, size_t outSize) {
    const WCHAR* lastSlash = wcsrchr(path, L'\\');
    const WCHAR* stem = lastSlash ? lastSlash + 1 : path;

    size_t len = wcslen(stem);
    for (size_t i = 0; i < len; i++) {
        if (stem[i] == L'.') {
            if (i > 0) len = i;
            break;
        }
    }

    if (len == 0 || len >= outSize) {
        wcscpy_s(out, outSize, L"Program");
        return;
    }

    CopyMemory(out, stem, len * sizeof(WCHAR));
    out[len] = L'\0';
}

// The section header is the identity of a program, so two programs must not end
// up with the same name: a "#2", "#3" ... suffix is appended until it is free.
static void MakeProgramNameUnique(WCHAR* name, int ignoreIndex) {
    WCHAR base[MAX_NAME_LEN];
    wcscpy_s(base, MAX_NAME_LEN, name);

    for (int n = 2; n < 1000; n++) {
        BOOL clash = FALSE;
        for (int i = 0; i < g_programCount; i++) {
            if (i == ignoreIndex) continue;
            if (_wcsicmp(g_programs[i].name, name) == 0) {
                clash = TRUE;
                break;
            }
        }
        if (!clash) return;

        WCHAR suffix[16];
        swprintf(suffix, 16, L"#%d", n);

        size_t baseLen = wcslen(base);
        size_t suffixLen = wcslen(suffix);
        size_t keep = (baseLen + suffixLen < MAX_NAME_LEN) ? baseLen : MAX_NAME_LEN - suffixLen - 1;
        CopyMemory(name, base, keep * sizeof(WCHAR));
        wcscpy_s(name + keep, MAX_NAME_LEN - keep, suffix);
    }
}

// Name and Main belong to the old numbered format: recognized so that they are
// dropped instead of being carried over as unknown keys.
static BOOL IsKnownProgramKey(const WCHAR* key) {
    static const WCHAR* const known[] = {
        L"Name", L"Path", L"Param", L"Start", L"Icon", L"Type", L"Mode", L"Window", L"Main"
    };
    for (int i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++) {
        if (_wcsicmp(key, known[i]) == 0) return TRUE;
    }
    return FALSE;
}

// Append one "text\r\n" line, dropping whatever no longer fits
static void AppendLine(WCHAR* dst, size_t size, const WCHAR* text) {
    size_t len = wcslen(dst);
    size_t add = wcslen(text);
    if (len + add + 2 >= size) return;

    CopyMemory(dst + len, text, add * sizeof(WCHAR));
    dst[len + add] = L'\r';
    dst[len + add + 1] = L'\n';
    dst[len + add + 2] = L'\0';
}

// Keep the comments of a program section and the keys this version does not know,
// so hand-written notes and foreign keys survive a rewrite of the file.
static void CollectSectionCarryOver(ProgramConfig* prog, IniFile* ini, const WCHAR* section) {
    WCHAR extra[MAX_EXTRA_LEN] = {0};
    WCHAR comments[MAX_COMMENT_LEN] = {0};

    for (int i = 0; i < ini->count; i++) {
        const IniEntry* entry = &ini->entries[i];
        if (!entry->section || _wcsicmp(entry->section, section) != 0) continue;

        if (!entry->key) {
            AppendLine(comments, MAX_COMMENT_LEN, entry->value);
            continue;
        }
        if (IsKnownProgramKey(entry->key)) continue;

        WCHAR line[MAX_NAME_LEN + MAX_PATH_LEN + 8] = {0};
        if (wcslen(entry->key) + wcslen(entry->value) + 3 >= sizeof(line) / sizeof(line[0])) continue;
        swprintf(line, sizeof(line) / sizeof(line[0]), L"%s=%s", entry->key, entry->value);
        AppendLine(extra, MAX_EXTRA_LEN, line);
    }

    wcscpy_s(prog->extra, MAX_EXTRA_LEN, extra);
    wcscpy_s(prog->comments, MAX_COMMENT_LEN, comments);
}

// Copy a value from the file into a field: over-long values are truncated instead
// of being dropped, which wcscpy_s would do.
static void CopyField(WCHAR* dst, size_t size, const WCHAR* src) {
    if (!src) {
        dst[0] = L'\0';
        return;
    }

    size_t len = wcslen(src);
    if (len >= size) len = size - 1;
    CopyMemory(dst, src, len * sizeof(WCHAR));
    dst[len] = L'\0';
}

static void LoadProgramFromSection(ProgramConfig* prog, IniFile* ini, const WCHAR* section, const WCHAR* path) {
    memset(prog, 0, sizeof(*prog));

    // The section header is the display name; configurations written by older
    // versions used a numbered section plus a Name key, which still works.
    const WCHAR* val = IniGet(ini, section, L"Name");
    if (!val || !*val) val = section;
    CopyField(prog->name, MAX_NAME_LEN, val);
    SanitizeProgramName(prog->name);
    if (prog->name[0] == L'\0') ProgramNameFromPath(path, prog->name, MAX_NAME_LEN);

    CopyField(prog->path, MAX_PATH_LEN, path);

    val = IniGet(ini, section, L"Param");  if (val) CopyField(prog->param, MAX_PARAM_LEN, val);
    val = IniGet(ini, section, L"Start");  if (val) CopyField(prog->start, MAX_PATH_LEN, val);
    val = IniGet(ini, section, L"Icon");   if (val) CopyField(prog->icon, MAX_PATH_LEN, val);
    val = IniGet(ini, section, L"Type");   if (val) CopyField(prog->type, MAX_TYPE_LEN, val);

    // A value equal to the default is not worth a line in the file
    val = IniGet(ini, section, L"Mode");
    prog->mode = (val && *val) ? ParseModeValue(val) : MODE_INHERIT;
    if (prog->mode == g_defaultMode) prog->mode = MODE_INHERIT;

    val = IniGet(ini, section, L"Window");
    prog->window = (val && *val) ? ParseWindowValue(val) : WINDOW_INHERIT;
    if (prog->window == g_defaultWindow) prog->window = WINDOW_INHERIT;

    CollectSectionCarryOver(prog, ini, section);
}

// Load programs from the config file (single-read in-memory parser)
void LoadPrograms() {
    GetIniFilePath(g_iniPath, MAX_PATH);
    g_programCount = 0;

    IniFile ini = {0};
    if (!IniLoad(&ini, g_iniPath)) return;

    // Defaults every program inherits unless it sets its own
    const WCHAR* val = IniGet(&ini, L"General", L"DefaultMode");
    g_defaultMode = (val && *val) ? ParseModeValue(val) : MODE_SEPARATE;
    val = IniGet(&ini, L"General", L"DefaultWindow");
    g_defaultWindow = (val && *val) ? ParseWindowValue(val) : WINDOW_NORMAL;

    // Sections are taken in the order they appear in the file, and a section is a
    // program when it has a non-empty Path key.
    const WCHAR* seen[INI_MAX_SECTIONS];
    int seenCount = 0;

    for (int i = 0; i < ini.count; i++) {
        const WCHAR* section = ini.entries[i].section;
        if (!section || !ini.entries[i].key) continue;
        if (seenCount >= INI_MAX_SECTIONS) break;

        BOOL known = FALSE;
        for (int s = 0; s < seenCount; s++) {
            if (_wcsicmp(seen[s], section) == 0) {
                known = TRUE;
                break;
            }
        }
        if (known) continue;
        seen[seenCount++] = section;

        const WCHAR* path = IniGet(&ini, section, L"Path");
        if (!path || !*path) continue;
        if (g_programCount >= MAX_PROGRAMS) break;

        LoadProgramFromSection(&g_programs[g_programCount], &ini, section, path);
        MakeProgramNameUnique(g_programs[g_programCount].name, -1);
        g_programCount++;
    }

    IniFree(&ini);
}

// The sections the program owns: the programs themselves plus the [General]
// defaults. Everything else in the file belongs to the user.
static BOOL IsManagedSection(IniFile* ini, const WCHAR* section, int sectionLen) {
    if (SectionEquals(section, sectionLen, L"General", 7)) return TRUE;

    const WCHAR* path = IniGetN(ini, section, sectionLen, L"Path");
    return path && *path;
}

// Copy every line the program does not manage: comments, blank lines, and all
// sections that are neither a program nor the [General] defaults.
static void CopyUnmanagedLines(TextBuf* tb, const WCHAR* text, IniFile* ini) {
    const WCHAR* section = NULL;
    int sectionLen = 0;
    BOOL skip = FALSE;

    const WCHAR* p = text;
    while (*p) {
        const WCHAR* lineEnd = p;
        while (*lineEnd && *lineEnd != L'\r' && *lineEnd != L'\n') lineEnd++;
        int lineLen = (int)(lineEnd - p);

        // Step over one line ending only, so that an empty line is a line of its own
        const WCHAR* next = lineEnd;
        if (*next == L'\r' && next[1] == L'\n') next += 2;
        else if (*next == L'\r' || *next == L'\n') next += 1;

        const WCHAR* s = p;
        while (*s == L' ' || *s == L'\t') s++;

        if (*s == L'[') {
            const WCHAR* e = wcschr(s, L']');
            if (e) {
                section = s + 1;
                sectionLen = (int)(e - s - 1);
                while (sectionLen > 0 && (section[sectionLen - 1] == L' ' || section[sectionLen - 1] == L'\t'))
                    sectionLen--;

                skip = IsManagedSection(ini, section, sectionLen);
            }
        }

        if (!skip) {
            TextBufAppendN(tb, p, lineLen);
            TextBufAppend(tb, L"\r\n");
        }

        p = next;
    }
}

// Sections are written after a single empty line, unless the file already ends
// with one (or is still empty).
static BOOL EnsureBlankLine(TextBuf* tb) {
    if (!tb->data || tb->len == 0) return TRUE;
    if (tb->len >= 4 && tb->data[tb->len - 1] == L'\n' && tb->data[tb->len - 2] == L'\r' &&
        tb->data[tb->len - 3] == L'\n') {
        return TRUE;
    }
    return TextBufAppend(tb, L"\r\n");
}

// The defaults every program inherits. It is written even when it holds the
// built-in values, so the file says where "nothing written" comes from.
static BOOL AppendGeneralSection(TextBuf* tb) {
    if (!EnsureBlankLine(tb)) return FALSE;
    if (!TextBufAppend(tb, L"[General]\r\n")) return FALSE;
    if (!TextBufAppendKey(tb, L"DefaultMode", ModeName(g_defaultMode))) return FALSE;
    return TextBufAppendKey(tb, L"DefaultWindow", WindowName(g_defaultWindow));
}

// Keys are written in the order the edit dialog shows them. Empty optional fields
// are left out, and so are Mode and Window when they are the default.
static BOOL AppendProgramSection(TextBuf* tb, const ProgramConfig* prog) {
    WCHAR header[MAX_NAME_LEN + 8];
    if (!EnsureBlankLine(tb)) return FALSE;

    swprintf(header, MAX_NAME_LEN + 8, L"[%s]\r\n", prog->name);
    if (!TextBufAppend(tb, header)) return FALSE;
    if (!TextBufAppend(tb, prog->comments)) return FALSE;
    if (!TextBufAppendKey(tb, L"Path", prog->path)) return FALSE;
    if (wcslen(prog->param) > 0 && !TextBufAppendKey(tb, L"Param", prog->param)) return FALSE;
    if (wcslen(prog->start) > 0 && !TextBufAppendKey(tb, L"Start", prog->start)) return FALSE;
    if (wcslen(prog->icon) > 0 && !TextBufAppendKey(tb, L"Icon", prog->icon)) return FALSE;
    if (prog->mode != MODE_INHERIT && !TextBufAppendKey(tb, L"Mode", ModeName(prog->mode))) return FALSE;
    if (prog->window != WINDOW_INHERIT && !TextBufAppendKey(tb, L"Window", WindowName(prog->window))) return FALSE;
    if (wcslen(prog->type) > 0 && !TextBufAppendKey(tb, L"Type", prog->type)) return FALSE;
    return TextBufAppend(tb, prog->extra);
}

// Save programs to the config file.
// The whole file image is built in memory and written with a single file write.
// The previous implementation issued one WritePrivateProfileStringW call per key,
// and each of those calls rewrites the complete file, which froze the UI for
// several seconds on every up/down click.
void SavePrograms() {
    // 1. Parse the file (which tokenizes its buffer) to learn which sections are managed
    IniFile ini = {0};
    WCHAR* parsedText = ReadTextFileW(g_iniPath);
    if (parsedText) IniParseText(&ini, parsedText);

    // 2. Copy everything the program does not manage
    TextBuf tb = {0};
    WCHAR* rawText = ReadTextFileW(g_iniPath);
    if (rawText && ini.entries) {
        CopyUnmanagedLines(&tb, rawText, &ini);
        HeapFree(GetProcessHeap(), 0, rawText);
    } else if (rawText) {
        HeapFree(GetProcessHeap(), 0, rawText);
    }

    // 3. The defaults, then one section per program in list order
    if (!AppendGeneralSection(&tb)) {
        TextBufFree(&tb);
        IniFree(&ini);
        return;
    }

    for (int i = 0; i < g_programCount; i++) {
        if (!AppendProgramSection(&tb, &g_programs[i])) {
            TextBufFree(&tb);
            IniFree(&ini);
            return;
        }
    }

    // 4. One single write
    WriteTextFileW(g_iniPath, tb.data, tb.len);
    TextBufFree(&tb);
    IniFree(&ini);
}

// Load icon from path (format: "path,index")
HICON LoadIconFromPath(const WCHAR* iconPath) {
    WCHAR expandedPath[MAX_PATH_LEN];
    WCHAR filePath[MAX_PATH_LEN];
    int iconIndex = 0;
    
    ExpandEnvStrings(iconPath, expandedPath, MAX_PATH_LEN);
    
    // Parse icon path and index
    wcscpy_s(filePath, MAX_PATH_LEN, expandedPath);
    WCHAR* comma = wcsrchr(filePath, L',');
    if (comma) {
        *comma = L'\0';
        iconIndex = _wtoi(comma + 1);
    }
    
    HICON hIcon = NULL;
    HICON hIconLarge = NULL;
    
    // Extract icon
    if (ExtractIconExW(filePath, iconIndex, &hIconLarge, &hIcon, 1) > 0) {
        if (hIconLarge) DestroyIcon(hIconLarge);
        return hIcon;
    }
    
    // Fallback: use default application icon
    return LoadIcon(NULL, IDI_APPLICATION);
}

// Initialize ListView
void InitListView(HWND hwnd) {
    g_hListView = GetDlgItem(hwnd, IDC_LISTVIEW);
    
    // Apply font
    SendMessageW(g_hListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Set extended styles
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    SendMessageW(g_hListView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, exStyle);
    
    // Create image list for icons (DPI-scaled)
    int iconSize = DPI_SCALE(16);
    g_hImageList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, 10, 10);
    SendMessageW(g_hListView, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)g_hImageList);
    
    // Add columns
    LVCOLUMNW col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    
    const WCHAR* headers[] = {L"图标", L"名称", L"路径", L"参数", L"启动路径",
                              L"打开方式", L"窗口模式", L"关联扩展名"};

    for (int i = 0; i < 8; i++) {
        col.pszText = (LPWSTR)headers[i];
        col.cx = g_settings.columnWidths[i];
        SendMessageW(g_hListView, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
    }
}

// Refresh a single ListView row in place from the program list.
// Used when rows are reordered so the whole list (and all icons) does not have to be
// rebuilt - and, unlike the old code, the icon is moved together with its row.
void UpdateListViewRow(int row) {
    if (!g_hListView || row < 0 || row >= g_programCount) return;

    ProgramConfig* prog = &g_programs[row];

    // Keep the row icon in sync with its data (-1 removes the image)
    LVITEMW item = {0};
    item.mask = LVIF_IMAGE;
    item.iItem = row;
    item.iImage = g_rowImage[row];
    SendMessageW(g_hListView, LVM_SETITEMW, 0, (LPARAM)&item);

    ListView_SetItemText(g_hListView, row, 1, prog->name);
    ListView_SetItemText(g_hListView, row, 2, prog->path);
    ListView_SetItemText(g_hListView, row, 3, prog->param);
    ListView_SetItemText(g_hListView, row, 4, prog->start);

    int mode = ProgramMode(prog);
    ListView_SetItemText(g_hListView, row, 5, mode == MODE_MERGE ? L"合并" : L"独立");

    int window = ProgramWindow(prog);
    ListView_SetItemText(g_hListView, row, 6, window == WINDOW_NORMAL ? L"常规"
                        : (window == WINDOW_MAX ? L"最大化" : L"最小化"));

    ListView_SetItemText(g_hListView, row, 7, prog->type);
}

// Add program to ListView
void AddProgramToListView(int index) {
    if (index < 0 || index >= g_programCount) return;
    
    ProgramConfig* prog = &g_programs[index];
    
    // Add icon to image list
    HICON hIcon = LoadIconFromPath(prog->icon);
    int imageIndex = ImageList_AddIcon(g_hImageList, hIcon);
    DestroyIcon(hIcon);
    g_rowImage[index] = imageIndex;
    
    // Insert item
    LVITEMW item = {0};
    item.mask = LVIF_TEXT | LVIF_IMAGE;
    item.iItem = index;
    item.iImage = imageIndex;
    item.pszText = L"";
    SendMessageW(g_hListView, LVM_INSERTITEMW, 0, (LPARAM)&item);
    
    // Set subitems
    UpdateListViewRow(index);
}

// Populate ListView with all programs
void PopulateListView() {
    SendMessageW(g_hListView, LVM_DELETEALLITEMS, 0, 0);
    ImageList_RemoveAll(g_hImageList);
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        g_rowImage[i] = -1;
    }
    
    for (int i = 0; i < g_programCount; i++) {
        AddProgramToListView(i);
    }
}

// Move one program from index 'from' to index 'to', shifting the rows in between.
// The icon index of each row moves with its data.
static void RotateProgramArray(int from, int to) {
    if (from == to) return;

    ProgramConfig tmpProg = g_programs[from];
    int tmpImage = g_rowImage[from];

    if (from < to) {
        for (int i = from; i < to; i++) {
            g_programs[i] = g_programs[i + 1];
            g_rowImage[i] = g_rowImage[i + 1];
        }
    } else {
        for (int i = from; i > to; i--) {
            g_programs[i] = g_programs[i - 1];
            g_rowImage[i] = g_rowImage[i - 1];
        }
    }

    g_programs[to] = tmpProg;
    g_rowImage[to] = tmpImage;
}

// Redraw only the rows in [from, to] (no full repopulation, no icon extraction)
static void RefreshListViewRows(int from, int to) {
    if (!g_hListView) return;
    if (from > to) { int t = from; from = to; to = t; }
    if (from < 0) from = 0;
    if (to >= g_programCount) to = g_programCount - 1;
    if (from > to) return;

    SendMessageW(g_hListView, WM_SETREDRAW, FALSE, 0);
    for (int i = from; i <= to; i++) {
        UpdateListViewRow(i);
    }
    SendMessageW(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, NULL, FALSE);
}

// Move the selected row up (delta = -1) or down (delta = +1) by one position
static void MoveSelectedProgram(int delta) {
    if (!g_hListView || g_programCount < 2) return;

    int sel = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
    if (sel < 0) return;

    int target = sel + delta;
    if (target < 0 || target >= g_programCount) return;

    RotateProgramArray(sel, target);
    RefreshListViewRows(sel, target);

    // Keep the moved row selected and visible
    ListView_SetItemState(g_hListView, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(g_hListView, target, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_hListView, target, FALSE);

    SavePrograms();
}

static BOOL CALLBACK SetFontEnumProc(HWND hwnd, LPARAM lParam) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

static void SetDialogFont(HWND hDlg, HFONT hFont) {
    EnumChildWindows(hDlg, SetFontEnumProc, (LPARAM)hFont);
}

// Convert HICON to HBITMAP for menu items
static HBITMAP IconToBitmap(HICON hIcon, int size) {
    if (!hIcon) return NULL;
    
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreenDC, size, size);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
    
    // Fill with white background
    RECT rc = {0, 0, size, size};
    HBRUSH hBr = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(hMemDC, &rc, hBr);
    
    // Draw icon centered
    int x = (size - GetSystemMetrics(SM_CXSMICON)) / 2;
    int y = (size - GetSystemMetrics(SM_CYSMICON)) / 2;
    DrawIconEx(hMemDC, x, y, hIcon, 0, 0, 0, NULL, DI_NORMAL);
    
    SelectObject(hMemDC, hOldBmp);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    
    return hBmp;
}

// Extract icons from file and show in icon list view
static void ExtractAndShowIcons(HWND hDlg, const WCHAR* filePath) {
    HWND hIconList = GetDlgItem(hDlg, IDD_ICON_LIST);
    if (!hIconList) return;
    
    // Clear existing
    ListView_DeleteAllItems(hIconList);
    HIMAGELIST hOld = (HIMAGELIST)SendMessageW(hIconList, LVM_GETIMAGELIST, LVSIL_NORMAL, 0);
    if (hOld) ImageList_Destroy(hOld);
    
    WCHAR expandedPath[MAX_PATH_LEN];
    ExpandEnvStrings(filePath, expandedPath, MAX_PATH_LEN);
    
    UINT iconCount = ExtractIconExW(expandedPath, -1, NULL, NULL, 0);
    if (iconCount == 0) return;
    if (iconCount > 200) iconCount = 200;
    
    int iconSize = DPI_SCALE(32);
    HIMAGELIST hImgList = ImageList_Create(iconSize, iconSize, ILC_COLOR32 | ILC_MASK, iconCount, 4);
    SendMessageW(hIconList, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)hImgList);
    
    for (UINT i = 0; i < iconCount; i++) {
        HICON hIcon = NULL;
        if (ExtractIconExW(expandedPath, i, &hIcon, NULL, 1) > 0 && hIcon) {
            ImageList_AddIcon(hImgList, hIcon);
            DestroyIcon(hIcon);
            
            LVITEMW item = {0};
            item.mask = LVIF_IMAGE;
            item.iItem = i;
            item.iImage = i;
            SendMessageW(hIconList, LVM_INSERTITEMW, 0, (LPARAM)&item);
        }
    }
    
    // Select first icon by default
    if (iconCount > 0) {
        ListView_SetItemState(hIconList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        g_selectedIconIndex = 0;
    }
}

// Edit dialog subclass procedure
LRESULT CALLBACK EditDialogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData) {
    switch (msg) {
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDD_BTN_BROWSE_PATH: {
                    OPENFILENAMEW ofn = {0};
                    WCHAR fileName[MAX_PATH] = {0};
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        SetDlgItemTextW(hwnd, IDD_EDIT_PATH, fileName);
                        
                        // Auto-fill icon file if empty
                        WCHAR iconFile[MAX_PATH_LEN];
                        GetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, iconFile, MAX_PATH_LEN);
                        if (wcslen(iconFile) == 0) {
                            SetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, fileName);
                            ExtractAndShowIcons(hwnd, fileName);
                        }
                    }
                    return 0;
                }
                
                case IDD_BTN_BROWSE_ICON_FILE: {
                    OPENFILENAMEW ofn = {0};
                    WCHAR fileName[MAX_PATH] = {0};
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"图标文件 (*.exe;*.dll;*.ico)\0*.exe;*.dll;*.ico\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        SetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, fileName);
                        ExtractAndShowIcons(hwnd, fileName);
                    }
                    return 0;
                }
                
                case IDD_EDIT_ICON_FILE: {
                    if (HIWORD(wParam) == EN_CHANGE) {
                        WCHAR iconFile[MAX_PATH_LEN];
                        GetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, iconFile, MAX_PATH_LEN);
                        if (wcslen(iconFile) > 0) {
                            ExtractAndShowIcons(hwnd, iconFile);
                        } else {
                            HWND hIconList = GetDlgItem(hwnd, IDD_ICON_LIST);
                            if (hIconList) {
                                ListView_DeleteAllItems(hIconList);
                                HIMAGELIST hOld = (HIMAGELIST)SendMessageW(hIconList, LVM_GETIMAGELIST, LVSIL_NORMAL, 0);
                                if (hOld) ImageList_Destroy(hOld);
                            }
                            g_selectedIconIndex = 0;
                            SetDlgItemTextW(hwnd, IDD_ICON_INDEX, L"0");
                        }
                    }
                    return 0;
                }
                
                case IDD_BTN_OK: {
                    ProgramConfig prog = {0};
                    
                    GetDlgItemTextW(hwnd, IDD_EDIT_NAME, prog.name, MAX_NAME_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_PATH, prog.path, MAX_PATH_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_PARAM, prog.param, MAX_PARAM_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_START, prog.start, MAX_PATH_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_TYPE, prog.type, MAX_TYPE_LEN);
                    
                    // Build icon string from icon file path and selected index
                    WCHAR iconFile[MAX_PATH_LEN];
                    GetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, iconFile, MAX_PATH_LEN);
                    if (wcslen(iconFile) > 0) {
                        swprintf(prog.icon, MAX_PATH_LEN, L"%s,%d", iconFile, g_selectedIconIndex);
                    }
                    
                    prog.mode = (int)SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_GETCURSEL, 0, 0);
                    prog.window = (int)SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_GETCURSEL, 0, 0);

                    if (wcslen(prog.path) == 0) {
                        MessageBoxW(hwnd, L"路径不能为空！", L"错误", MB_OK | MB_ICONERROR);
                        return 0;
                    }

                    // The name doubles as the section header of the program, so keep
                    // it usable as one, and unique among the other programs
                    SanitizeProgramName(prog.name);
                    if (prog.name[0] == L'\0') ProgramNameFromPath(prog.path, prog.name, MAX_NAME_LEN);

                    if (g_editIndex >= 0 && g_editIndex < g_programCount) {
                        // The section keeps its comments and its foreign keys
                        wcscpy_s(prog.extra, MAX_EXTRA_LEN, g_programs[g_editIndex].extra);
                        wcscpy_s(prog.comments, MAX_COMMENT_LEN, g_programs[g_editIndex].comments);
                        g_programs[g_editIndex] = prog;
                        MakeProgramNameUnique(g_programs[g_editIndex].name, g_editIndex);
                    } else {
                        if (g_programCount < MAX_PROGRAMS) {
                            g_programs[g_programCount] = prog;
                            MakeProgramNameUnique(g_programs[g_programCount].name, -1);
                            g_programCount++;
                        } else {
                            MessageBoxW(hwnd, L"已达到最大程序数量限制！", L"错误", MB_OK | MB_ICONERROR);
                            return 0;
                        }
                    }
                    
                    SavePrograms();
                    
                    RemoveWindowSubclass(hwnd, EditDialogSubclassProc, subclassId);
                    DestroyWindow(hwnd);
                    return 0;
                }
                
                case IDD_BTN_CANCEL:
                    g_prefillPath[0] = L'\0';
                    g_prefillName[0] = L'\0';
                    g_prefillStart[0] = L'\0';
                    g_prefillType[0] = L'\0';
                    RemoveWindowSubclass(hwnd, EditDialogSubclassProc, subclassId);
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        }
        
        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            if (nmhdr->idFrom == IDD_ICON_LIST) {
                if (nmhdr->code == LVN_ITEMCHANGED) {
                    LPNMLISTVIEW nmlv = (LPNMLISTVIEW)lParam;
                    if (nmlv->uNewState & LVIS_SELECTED) {
                        g_selectedIconIndex = nmlv->iItem;
                        WCHAR indexStr[16];
                        swprintf(indexStr, 16, L"%d", g_selectedIconIndex);
                        SetDlgItemTextW(hwnd, IDD_ICON_INDEX, indexStr);
                    }
                }
            }
            break;
        }
        
        case WM_CLOSE:
            g_prefillPath[0] = L'\0';
            g_prefillName[0] = L'\0';
            g_prefillStart[0] = L'\0';
            g_prefillType[0] = L'\0';
            RemoveWindowSubclass(hwnd, EditDialogSubclassProc, subclassId);
            DestroyWindow(hwnd);
            return 0;
    }
    
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Show edit dialog
void ShowEditDialog(HWND parent, int index) {
    g_selectedIconIndex = 0;
    
    // Dialog size is derived from the actual control layout below:
    // - height: content rows accumulated, buttons anchored to the bottom edge
    // - width:  content (margin + label + edit + margin), corrected after
    //   creation by measuring the real frame, so the right margin mirrors
    //   the left one regardless of theme metrics
    int margin = DPI_SCALE(10);   // top/bottom/left/right content margin
    int labelWidth = DPI_SCALE(80);
    int editWidth = DPI_SCALE(350);
    int spacing = DPI_SCALE(30);  // single-line row height
    int editH = DPI_SCALE(22);
    int btnH = DPI_SCALE(25);
    // Icon preview list: one row of large (32px) icons plus the item label
    // underneath and the view's own padding, so the preview fits its content.
    int iconSize = DPI_SCALE(32);
    int iconListH = iconSize + DPI_SCALE(24);
    // Extension edit: 3-line multiline edit
    int typeH = editH * 3 + DPI_SCALE(8);
    // Dialog client height: 7 single-line rows, then the icon list, the
    // extension edit, a gap, the button row and the bottom margin.
    // The OK/Cancel buttons are anchored to the bottom of the dialog below.
    int clientH = margin
                + spacing * 7
                + iconListH + DPI_SCALE(5)
                + typeH
                + DPI_SCALE(10)
                + btnH
                + margin;
    int clientW = margin + labelWidth + editWidth + margin;

    // Create with an initial guess, then measure the actual frame with
    // AdjustWindowRect and resize so the client area matches clientW x clientH
    // exactly (avoids theme/DPI frame-metric surprises).
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",
        L"编辑程序",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, clientW + DPI_SCALE(20), clientH + DPI_SCALE(60),
        parent, NULL, g_hInst, NULL
    );
    if (!hwnd) return;

    RECT frameRc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&frameRc, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
                       FALSE, WS_EX_DLGMODALFRAME);
    SetWindowPos(hwnd, NULL, 0, 0,
                 frameRc.right - frameRc.left, frameRc.bottom - frameRc.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    
    if (!hwnd) return;
    
    int y = margin;
    int lx = margin;
    int browseW = DPI_SCALE(60);
    int comboW = DPI_SCALE(150);
    int comboH = DPI_SCALE(100);
    
    // 名称
    CreateWindowW(L"STATIC", L"名称:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth, editH, hwnd, (HMENU)IDD_EDIT_NAME, g_hInst, NULL);
    y += spacing;
    
    // 路径
    CreateWindowW(L"STATIC", L"路径:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth - browseW - DPI_SCALE(5), editH, hwnd, (HMENU)IDD_EDIT_PATH, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        lx + labelWidth + editWidth - browseW, y, browseW, editH, hwnd, (HMENU)IDD_BTN_BROWSE_PATH, g_hInst, NULL);
    y += spacing;
    
    // 参数
    CreateWindowW(L"STATIC", L"参数:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth, editH, hwnd, (HMENU)IDD_EDIT_PARAM, g_hInst, NULL);
    y += spacing;
    
    // 启动路径
    CreateWindowW(L"STATIC", L"启动路径:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth, editH, hwnd, (HMENU)IDD_EDIT_START, g_hInst, NULL);
    y += spacing;
    
    // 打开方式
    CreateWindowW(L"STATIC", L"打开方式:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    HWND hComboMode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        lx + labelWidth, y, comboW, comboH, hwnd, (HMENU)IDD_COMBO_MODE, g_hInst, NULL);
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"独立");
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"合并");
    y += spacing;
    
    // 窗口模式
    CreateWindowW(L"STATIC", L"窗口模式:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    HWND hComboWindow = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        lx + labelWidth, y, comboW, comboH, hwnd, (HMENU)IDD_COMBO_WINDOW, g_hInst, NULL);
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"常规");
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"最大化");
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"最小化");
    y += spacing;
    
    // 图标文件
    CreateWindowW(L"STATIC", L"图标文件:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth - browseW - DPI_SCALE(5), editH, hwnd, (HMENU)IDD_EDIT_ICON_FILE, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        lx + labelWidth + editWidth - browseW, y, browseW, editH, hwnd, (HMENU)IDD_BTN_BROWSE_ICON_FILE, g_hInst, NULL);
    y += spacing;
    
    // 图标 (index label + icon list)
    CreateWindowW(L"STATIC", L"图标:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowW(L"STATIC", L"0", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, lx + labelWidth, y, DPI_SCALE(30), editH, hwnd, (HMENU)IDD_ICON_INDEX, g_hInst, NULL);
    
    HWND hIconList = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_BORDER,
        lx + labelWidth + DPI_SCALE(35), y, editWidth - DPI_SCALE(35), iconListH,
        hwnd, (HMENU)IDD_ICON_LIST, g_hInst, NULL);
    SendMessageW(hIconList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_DOUBLEBUFFER);
    y += iconListH + DPI_SCALE(5);
    
    // 扩展名 (multiline, 3 lines)
    CreateWindowW(L"STATIC", L"扩展名:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                    lx + labelWidth, y, editWidth, typeH, hwnd, (HMENU)IDD_EDIT_TYPE, g_hInst, NULL);
    
    // 按钮 (anchored to the bottom edge of the client area, no dead space below;
    // right edge of Cancel aligns with the content's right edge)
    int btnRowY = clientH - margin - btnH;
    CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        lx + labelWidth + editWidth - DPI_SCALE(160), btnRowY, DPI_SCALE(70), btnH, hwnd, (HMENU)IDD_BTN_OK, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        lx + labelWidth + editWidth - DPI_SCALE(80), btnRowY, DPI_SCALE(70), btnH, hwnd, (HMENU)IDD_BTN_CANCEL, g_hInst, NULL);
    
    SetDialogFont(hwnd, g_hFont);
    
    SetWindowSubclass(hwnd, EditDialogSubclassProc, 0, 0);
    
    if (index >= 0 && index < g_programCount) {
        ProgramConfig* prog = &g_programs[index];
        SetDlgItemTextW(hwnd, IDD_EDIT_NAME, prog->name);
        SetDlgItemTextW(hwnd, IDD_EDIT_PATH, prog->path);
        SetDlgItemTextW(hwnd, IDD_EDIT_PARAM, prog->param);
        SetDlgItemTextW(hwnd, IDD_EDIT_START, prog->start);
        SetDlgItemTextW(hwnd, IDD_EDIT_TYPE, prog->type);
        SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, ProgramMode(prog), 0);
        SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, ProgramWindow(prog), 0);
        
        // Parse icon string "path,index"
        WCHAR* comma = wcsrchr(prog->icon, L',');
        if (comma) {
            *comma = L'\0';
            g_selectedIconIndex = _wtoi(comma + 1);
            SetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, prog->icon);
            WCHAR idxStr[16];
            swprintf(idxStr, 16, L"%d", g_selectedIconIndex);
            SetDlgItemTextW(hwnd, IDD_ICON_INDEX, idxStr);
            ExtractAndShowIcons(hwnd, prog->icon);
            *comma = L',';
            
            // Select the correct icon
            if (g_selectedIconIndex >= 0) {
                ListView_SetItemState(hIconList, g_selectedIconIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        }
    } else if (wcslen(g_prefillPath) > 0) {
        SetDlgItemTextW(hwnd, IDD_EDIT_NAME, g_prefillName);
        SetDlgItemTextW(hwnd, IDD_EDIT_PATH, g_prefillPath);
        SetDlgItemTextW(hwnd, IDD_EDIT_START, g_prefillStart);
        SetDlgItemTextW(hwnd, IDD_EDIT_ICON_FILE, g_prefillPath);
        ExtractAndShowIcons(hwnd, g_prefillPath);
        if (wcslen(g_prefillType) > 0) {
            SetDlgItemTextW(hwnd, IDD_EDIT_TYPE, g_prefillType);
        }
        SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, 0, 0);
    } else {
        SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, 0, 0);
        SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, 0, 0);
    }
    
    g_editIndex = index;
    
    EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwnd)) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    PopulateListView();
}

// Delete selected programs
void DeleteSelectedPrograms(HWND hwnd) {
    int selectedCount = (int)SendMessageW(g_hListView, LVM_GETSELECTEDCOUNT, 0, 0);
    
    if (selectedCount == 0) {
        MessageBoxW(hwnd, L"请先选择要删除的程序！", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    WCHAR msg[256];
    swprintf(msg, 256, L"确定要删除选中的 %d 个程序吗？", selectedCount);
    
    if (MessageBoxW(hwnd, msg, L"确认删除", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    // Collect selected indices
    int indices[MAX_PROGRAMS];
    int count = 0;
    int idx = -1;
    while ((idx = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, idx, LVNI_SELECTED)) != -1) {
        indices[count++] = idx;
    }
    
    // Sort descending so we delete from end to beginning
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (indices[i] < indices[j]) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }
    
    // Delete from highest index to lowest
    for (int i = 0; i < count; i++) {
        int delIdx = indices[i];
        if (delIdx >= 0 && delIdx < g_programCount) {
            for (int j = delIdx; j < g_programCount - 1; j++) {
                g_programs[j] = g_programs[j + 1];
            }
            g_programCount--;
        }
    }
    
    PopulateListView();
    SavePrograms();
}

// Show about dialog
void ShowAboutDialog(HWND parent) {
    WCHAR msg[512];
    // Extract year from compile date (e.g. "Jun 24 2026" -> "2026")
    const char* date = __DATE__;
    const char* yearStr = date + strlen(date) - 4;
    int year = atoi(yearStr);
    
    swprintf(msg, 512,
        L"F4Menu v%S\n\n"
        L"Windows 扩展名关联与文件打开工具\n\n"
        L"使用纯 Win32 API 开发\n"
        L"兼容 Windows XP 到 Windows 11\n\n"
        L"© %d",
        F4MENU_VERSION_A, year);
    
    MessageBoxW(parent, msg, L"关于 F4Menu", MB_OK | MB_ICONINFORMATION);
}

// Add program from exe file path (used by button and drag-and-drop)
void AddProgramFromFilePath(HWND hwnd, const WCHAR* exePath) {
    WCHAR* lastDot = wcsrchr(exePath, L'.');
    if (lastDot && _wcsicmp(lastDot, L".exe") == 0) {
        WCHAR prodName[MAX_NAME_LEN] = {0};
        if (GetExeProductName(exePath, prodName, MAX_NAME_LEN) && wcslen(prodName) > 0) {
            wcscpy_s(g_prefillName, MAX_NAME_LEN, prodName);
        } else {
            WCHAR* lastSlash = wcsrchr(exePath, L'\\');
            WCHAR* name = lastSlash ? lastSlash + 1 : exePath;
            wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
            WCHAR* dot = wcsrchr(g_prefillName, L'.');
            if (dot) *dot = L'\0';
        }
    } else {
        WCHAR* lastSlash = wcsrchr(exePath, L'\\');
        WCHAR* name = lastSlash ? lastSlash + 1 : exePath;
        wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
        WCHAR* dot = wcsrchr(g_prefillName, L'.');
        if (dot) *dot = L'\0';
    }

    wcscpy_s(g_prefillPath, MAX_PATH_LEN, exePath);
    g_prefillStart[0] = L'\0';
    g_prefillType[0] = L'\0';

    ShowEditDialog(hwnd, -1);

    g_prefillPath[0] = L'\0';
    g_prefillName[0] = L'\0';
    g_prefillType[0] = L'\0';
}

// Main window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create ListView
            HWND hListView = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER,
                DPI_SCALE(10), DPI_SCALE(10), DPI_SCALE(760), DPI_SCALE(480),
                hwnd, (HMENU)IDC_LISTVIEW, g_hInst, NULL
            );
            
            InitListView(hwnd);
            PopulateListView();
            
            // Create buttons (initial position only; WM_SIZE re-anchors the row
            // to the bottom edge with the Exit button aligned to the list's right)
            int btnY = DPI_SCALE(500);
            int btnX = DPI_SCALE(10);
            int btnWidth = DPI_SCALE(65);
            int btnHeight = DPI_SCALE(30);
            int btnSpacing = DPI_SCALE(75);
            
            CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_ADD, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_EDIT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_DELETE, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"上移", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_UP, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"下移", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_DOWN, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_ABOUT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"退出", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_EXIT, g_hInst, NULL);
            
            SetDialogFont(hwnd, g_hFont);
            
            DragAcceptFiles(hwnd, TRUE);
            
            return 0;
        }
        
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            // Button row metrics; a DPI_SCALE(10) gap separates the list from
            // the buttons (mirroring the DPI_SCALE(10) outer margins).
            int btnHeight = DPI_SCALE(30);
            int btnGap = DPI_SCALE(10);
            int btnRowReserve = btnGap + btnHeight + DPI_SCALE(10); // gap + row + bottom margin

            // Resize ListView so its bottom edge sits btnGap above the button row
            SetWindowPos(g_hListView, NULL, DPI_SCALE(10), DPI_SCALE(10),
                         width - DPI_SCALE(20), height - btnRowReserve - DPI_SCALE(10), SWP_NOZORDER);

            // Button row: Add anchors to the ListView's left edge, Exit to its
            // right edge; the five middle buttons spread evenly in between.
            int btnY = height - btnRowReserve + btnGap;
            int btnLeft = DPI_SCALE(10);                   // matches ListView left edge
            int btnRight = width - DPI_SCALE(10);          // matches ListView right edge
            int btnW = DPI_SCALE(65);
            int btnCount = 7;

            // Even spacing: each button's left edge = left + i * step,
            // except the last which is flush right (step absorbed by widths).
            int span = btnRight - btnLeft - btnW;          // travel of the button's left edge
            int step = span / (btnCount - 1);

            const int btnIds[7] = {
                IDC_BTN_ADD, IDC_BTN_EDIT, IDC_BTN_DELETE, IDC_BTN_UP,
                IDC_BTN_DOWN, IDC_BTN_ABOUT, IDC_BTN_EXIT
            };
            for (int i = 0; i < btnCount; i++) {
                int bx = btnLeft + (i == btnCount - 1 ? span : step * i);
                SetWindowPos(GetDlgItem(hwnd, btnIds[i]), NULL, bx, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD: {
                    OPENFILENAMEW ofn = {0};
                    WCHAR fileName[MAX_PATH] = {0};
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrTitle = L"选择要添加的程序";
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        AddProgramFromFilePath(hwnd, fileName);
                    }
                    return 0;
                }
                
                case IDC_BTN_EDIT: {
                    int selected = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (selected >= 0) {
                        ShowEditDialog(hwnd, selected);
                    } else {
                        MessageBoxW(hwnd, L"请先选择要编辑的程序！", L"提示", MB_OK | MB_ICONINFORMATION);
                    }
                    return 0;
                }
                
                case IDC_BTN_DELETE:
                    DeleteSelectedPrograms(hwnd);
                    return 0;
                
                case IDC_BTN_UP: {
                    MoveSelectedProgram(-1);
                    return 0;
                }
                
                case IDC_BTN_DOWN: {
                    MoveSelectedProgram(1);
                    return 0;
                }
                
                case IDC_BTN_ABOUT:
                    ShowAboutDialog(hwnd);
                    return 0;
                
                case IDC_BTN_EXIT:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        }
        
        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            if (nmhdr->idFrom == IDC_LISTVIEW) {
                if (nmhdr->code == NM_DBLCLK) {
                    int selected = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                    if (selected >= 0) {
                        ShowEditDialog(hwnd, selected);
                    }
                } else if (nmhdr->code == LVN_BEGINDRAG) {
                    LPNMLISTVIEW nmlv = (LPNMLISTVIEW)lParam;
                    g_dragging = TRUE;
                    g_dragIndex = nmlv->iItem;
                    g_dragTarget = -1;

                    g_dragImageList = (HIMAGELIST)SendMessageW(g_hListView, LVM_CREATEDRAGIMAGE, nmlv->iItem, 0);
                    POINT pt = {nmlv->ptAction.x, nmlv->ptAction.y};
                    ClientToScreen(g_hListView, &pt);
                    ImageList_DragEnter(GetDesktopWindow(), pt.x, pt.y);
                    ImageList_DragShowNolock(TRUE);
                    SetCapture(hwnd);
                }
            }
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (g_dragging) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                ImageList_DragMove(x, y);

                POINT pt = {x, y};
                MapWindowPoints(hwnd, g_hListView, &pt, 1);

                int prevTarget = g_dragTarget;
                LVHITTESTINFO hit = {0};
                hit.pt = pt;
                g_dragTarget = (int)SendMessageW(g_hListView, LVM_HITTEST, 0, (LPARAM)&hit);

                if (g_dragTarget != prevTarget) {
                    if (prevTarget >= 0)
                        ListView_SetItemState(g_hListView, prevTarget, 0, LVIS_CUT);
                    if (g_dragTarget >= 0 && g_dragTarget != g_dragIndex)
                        ListView_SetItemState(g_hListView, g_dragTarget, LVIS_CUT, LVIS_CUT);
                }
            }
            return 0;
        }
        
        case WM_LBUTTONUP: {
            if (g_dragging) {
                g_dragging = FALSE;
                ImageList_DragLeave(GetDesktopWindow());
                ImageList_DragShowNolock(FALSE);
                if (g_dragImageList) {
                    ImageList_Destroy(g_dragImageList);
                    g_dragImageList = NULL;
                }
                ReleaseCapture();

                if (g_dragTarget >= 0 && g_dragTarget != g_dragIndex && g_dragTarget < g_programCount) {
                    // Reorder the data and refresh only the affected rows
                    RotateProgramArray(g_dragIndex, g_dragTarget);
                    RefreshListViewRows(g_dragIndex, g_dragTarget);

                    SavePrograms();
                    ListView_SetItemState(g_hListView, g_dragTarget, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }

                // Clear the drag highlight
                ListView_SetItemState(g_hListView, -1, 0, LVIS_CUT);
                g_dragTarget = -1;
            }
            return 0;
        }
        
        case WM_GETMINMAXINFO: {
            // Keep the window large enough for the button row (about 525 px
            // wide for seven buttons) and the list plus button row vertically
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = DPI_SCALE(540);
            mmi->ptMinTrackSize.y = DPI_SCALE(350);
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                WCHAR filePath[MAX_PATH];
                DragQueryFileW(hDrop, i, filePath, MAX_PATH);
                WCHAR* ext = wcsrchr(filePath, L'.');
                if (ext && _wcsicmp(ext, L".exe") == 0) {
                    AddProgramFromFilePath(hwnd, filePath);
                }
            }
            DragFinish(hDrop);
            return 0;
        }
        
        case WM_CLOSE: {
            // Save window position and size
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_settings.winPosX = rc.left;
            g_settings.winPosY = rc.top;
            g_settings.winWidth = rc.right - rc.left;
            g_settings.winHeight = rc.bottom - rc.top;
            SaveSettings();
            
            DestroyWindow(hwnd);
            return 0;
        }
        
        case WM_DESTROY:
            if (g_hImageList) {
                ImageList_Destroy(g_hImageList);
            }
            if (g_hFont) {
                DeleteObject(g_hFont);
                g_hFont = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Configuration mode
int ConfigMode() {
    LoadSettings();
    LoadPrograms();
    
    // Register window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"F4MenuConfig";
    wc.hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(1));
    wc.hIconSm = LoadIcon(g_hInst, MAKEINTRESOURCE(1));
    
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"窗口类注册失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // Create main window
    // Clamp the saved window rectangle to the monitor containing its title
    // bar.  Without this, a saved position from a previous (larger) monitor
    // setup can leave the bottom button row off-screen.
    int cw = g_settings.winWidth;
    int ch = g_settings.winHeight;
    int cx = g_settings.winPosX;
    int cy = g_settings.winPosY;

    HMONITOR hMon = MonitorFromPoint(
        (POINT){ cx + 100, cy + 20 }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        int workW = mi.rcWork.right - mi.rcWork.left;
        int workH = mi.rcWork.bottom - mi.rcWork.top;
        if (cw > workW) cw = workW;
        if (ch > workH) ch = workH;
        if (cw < DPI_SCALE(540)) cw = DPI_SCALE(540);
        if (ch < DPI_SCALE(350)) ch = DPI_SCALE(350);
        if (cx > mi.rcWork.right - cw) cx = mi.rcWork.right - cw;
        if (cy > mi.rcWork.bottom - ch) cy = mi.rcWork.bottom - ch;
        if (cx < mi.rcWork.left) cx = mi.rcWork.left;
        if (cy < mi.rcWork.top) cy = mi.rcWork.top;
    }

    HWND hwnd = CreateWindowExW(
        0,
        L"F4MenuConfig",
        L"F4Menu - 配置",
        WS_OVERLAPPEDWINDOW,
        cx, cy,
        cw, ch,
        NULL, NULL, g_hInst, NULL
    );
    
    if (!hwnd) {
        MessageBoxW(NULL, L"窗口创建失败！", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return (int)msg.wParam;
}

// Match file extension with type list
BOOL MatchExtension(const WCHAR* fileExt, const WCHAR* typeList) {
    if (!fileExt || !typeList) return FALSE;
    
    // Check for wildcard
    if (wcschr(typeList, L'*')) return TRUE;
    
    // Parse type list
    WCHAR types[MAX_TYPE_LEN];
    wcscpy_s(types, MAX_TYPE_LEN, typeList);
    
    WCHAR* context = NULL;
    WCHAR* token = wcstok(types, L",", &context);
    while (token) {
        // Trim whitespace
        while (*token == L' ') token++;
        
        if (_wcsicmp(token, fileExt) == 0) {
            return TRUE;
        }
        
        token = wcstok(NULL, L",", &context);
    }
    
    return FALSE;
}

// Execute program with files
void ExecuteProgram(ProgramConfig* prog, WCHAR** files, int fileCount) {
    WCHAR expandedPath[MAX_PATH_LEN];
    WCHAR expandedStart[MAX_PATH_LEN];
    WCHAR expandedParam[MAX_PARAM_LEN];
    
    ExpandEnvStrings(prog->path, expandedPath, MAX_PATH_LEN);
    ExpandEnvStrings(prog->start, expandedStart, MAX_PATH_LEN);
    ExpandEnvStrings(prog->param, expandedParam, MAX_PARAM_LEN);
    
    // Determine window state
    int nShowCmd = SW_SHOWNORMAL;
    int windowMode = ProgramWindow(prog);
    if (windowMode == WINDOW_MAX) nShowCmd = SW_SHOWMAXIMIZED;
    else if (windowMode == WINDOW_MIN) nShowCmd = SW_SHOWMINIMIZED;
    
    if (ProgramMode(prog) == MODE_SEPARATE) {
        // Independent mode - launch for each file
        for (int i = 0; i < fileCount; i++) {
            WCHAR cmdLine[MAX_PATH_LEN * 2];
            swprintf(cmdLine, MAX_PATH_LEN * 2, L"\"%s\" %s \"%s\"", 
                expandedPath, expandedParam, files[i]);
            
            SHELLEXECUTEINFOW sei = {0};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpFile = expandedPath;
            sei.lpParameters = cmdLine + wcslen(expandedPath) + 3;
            sei.lpDirectory = wcslen(expandedStart) > 0 ? expandedStart : NULL;
            sei.nShow = nShowCmd;
            
            if (!ShellExecuteExW(&sei)) {
                WCHAR errMsg[512];
                swprintf(errMsg, 512, L"无法启动程序：%s\n文件：%s", prog->name, files[i]);
                MessageBoxW(NULL, errMsg, L"错误", MB_OK | MB_ICONERROR);
            }
        }
    } else {
        // Merged mode - launch once with all files
        WCHAR cmdLine[MAX_PATH_LEN * 4] = {0};
        swprintf(cmdLine, MAX_PATH_LEN * 4, L"\"%s\" %s", expandedPath, expandedParam);
        
        for (int i = 0; i < fileCount; i++) {
            wcscat_s(cmdLine, MAX_PATH_LEN * 4, L" \"");
            wcscat_s(cmdLine, MAX_PATH_LEN * 4, files[i]);
            wcscat_s(cmdLine, MAX_PATH_LEN * 4, L"\"");
        }
        
        SHELLEXECUTEINFOW sei = {0};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = expandedPath;
        sei.lpParameters = cmdLine + wcslen(expandedPath) + 3;
        sei.lpDirectory = wcslen(expandedStart) > 0 ? expandedStart : NULL;
        sei.nShow = nShowCmd;
        
        if (!ShellExecuteExW(&sei)) {
            WCHAR errMsg[512];
            swprintf(errMsg, 512, L"无法启动程序：%s", prog->name);
            MessageBoxW(NULL, errMsg, L"错误", MB_OK | MB_ICONERROR);
        }
    }
}

// Icon bitmap cache for LaunchMode menu (avoids duplicate ExtractIconExW calls)
#define ICON_CACHE_SIZE 256

typedef struct {
    WCHAR path[MAX_PATH_LEN];
    HBITMAP hBitmap;
} IconCacheEntry;

static IconCacheEntry g_iconCache[ICON_CACHE_SIZE];
static int g_iconCacheCount = 0;

static HBITMAP GetCachedIconBitmap(const WCHAR* iconPath, int size) {
    for (int i = 0; i < g_iconCacheCount; i++) {
        if (_wcsicmp(g_iconCache[i].path, iconPath) == 0)
            return g_iconCache[i].hBitmap;
    }

    HICON hIcon = LoadIconFromPath(iconPath);
    HBITMAP hBitmap = IconToBitmap(hIcon, size);
    if (hIcon) DestroyIcon(hIcon);

    if (g_iconCacheCount < ICON_CACHE_SIZE) {
        wcscpy_s(g_iconCache[g_iconCacheCount].path, MAX_PATH_LEN, iconPath);
        g_iconCache[g_iconCacheCount].hBitmap = hBitmap;
        g_iconCacheCount++;
    }

    return hBitmap;
}

static void FreeIconCache() {
    for (int i = 0; i < g_iconCacheCount; i++) {
        if (g_iconCache[i].hBitmap) DeleteObject(g_iconCache[i].hBitmap);
        g_iconCache[i].path[0] = L'\0';
    }
    g_iconCacheCount = 0;
}

// The popup menu is tracked in a nested message loop.  Explicitly end that
// loop when its owner is deactivated; otherwise a click in another application
// can leave the menu on screen after the owner has lost the foreground.
static LRESULT CALLBACK PopupHostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) EndMenu();
            break;

        case WM_ACTIVATEAPP:
            if (!wParam) EndMenu();
            break;

        case WM_CANCELMODE:
        case WM_KILLFOCUS:
            EndMenu();
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Launch mode
void LaunchMode(int argc, WCHAR** argv) {
    LoadPrograms();
    
    if (argc < 1) return;
    
    // Files are directly in argv
    WCHAR** files = argv;
    int fileCount = argc;
    
    // Find matching programs
    int matchedPrograms[MAX_PROGRAMS];
    int matchCount = 0;
    
    for (int i = 0; i < g_programCount; i++) {
        BOOL matches = FALSE;
        
        for (int j = 0; j < fileCount; j++) {
            WCHAR* ext = wcsrchr(files[j], L'.');
            if (ext) {
                ext++; // Skip the dot
                if (MatchExtension(ext, g_programs[i].type)) {
                    matches = TRUE;
                    break;
                }
            } else if (MatchExtension(L"", g_programs[i].type)) {
                matches = TRUE;
                break;
            }
        }
        
        if (matches) {
            matchedPrograms[matchCount++] = i;
        }
    }
    
    if (matchCount == 0) {
        MessageBoxW(NULL, L"没有找到匹配的程序！", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    // Create popup menu
    HMENU hMenu = CreatePopupMenu();
    HMENU hSubMenuAll = CreatePopupMenu();
    int menuIconSize = DPI_SCALE(16);
    
    for (int i = 0; i < matchCount; i++) {
        int progIdx = matchedPrograms[i];
        
        MENUITEMINFOW mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
        mii.wID = progIdx + 1;
        mii.dwTypeData = g_programs[progIdx].name;
        
        mii.hbmpItem = GetCachedIconBitmap(g_programs[progIdx].icon, menuIconSize);
        
        WCHAR expandedPath[MAX_PATH_LEN];
        ExpandEnvStrings(g_programs[progIdx].path, expandedPath, MAX_PATH_LEN);
        if (GetFileAttributesW(expandedPath) == INVALID_FILE_ATTRIBUTES) {
            mii.fMask |= MIIM_STATE;
            mii.fState = MFS_DISABLED | MFS_GRAYED;
        }
        
        InsertMenuItemW(hMenu, i, TRUE, &mii);
    }
    
    // Add separator
    MENUITEMINFOW sep = {0};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItemW(hMenu, matchCount, TRUE, &sep);
    
    // "更多程序" submenu - list all programs
    for (int i = 0; i < g_programCount; i++) {
        MENUITEMINFOW mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP | MIIM_STATE;
        mii.wID = 10000 + i;
        mii.dwTypeData = g_programs[i].name;
        
        WCHAR expandedPath[MAX_PATH_LEN];
        ExpandEnvStrings(g_programs[i].path, expandedPath, MAX_PATH_LEN);
        if (GetFileAttributesW(expandedPath) == INVALID_FILE_ATTRIBUTES) {
            mii.fState = MFS_DISABLED | MFS_GRAYED;
        }
        
        mii.hbmpItem = GetCachedIconBitmap(g_programs[i].icon, menuIconSize);
        
        InsertMenuItemW(hSubMenuAll, i, TRUE, &mii);
    }
    
    MENUITEMINFOW miiMore = {0};
    miiMore.cbSize = sizeof(miiMore);
    miiMore.fMask = MIIM_STRING | MIIM_SUBMENU;
    miiMore.hSubMenu = hSubMenuAll;
    miiMore.dwTypeData = L"更多程序";
    InsertMenuItemW(hMenu, matchCount + 1, TRUE, &miiMore);
    
    // Add separator
    InsertMenuItemW(hMenu, matchCount + 2, TRUE, &sep);
    
    // "其它程序..." item
    MENUITEMINFOW miiOther = {0};
    miiOther.cbSize = sizeof(miiOther);
    miiOther.fMask = MIIM_STRING | MIIM_ID;
    miiOther.wID = 9001;
    miiOther.dwTypeData = L"其它程序...";
    InsertMenuItemW(hMenu, matchCount + 3, TRUE, &miiOther);

    // Add separator
    InsertMenuItemW(hMenu, matchCount + 4, TRUE, &sep);

    // "设置" item
    MENUITEMINFOW miiSettings = {0};
    miiSettings.cbSize = sizeof(miiSettings);
    miiSettings.fMask = MIIM_STRING | MIIM_ID;
    miiSettings.wID = 9002;
    miiSettings.dwTypeData = L"设置";
    InsertMenuItemW(hMenu, matchCount + 5, TRUE, &miiSettings);

    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);
    
    // Create a real, activatable top-level window as the menu owner.  A
    // WS_EX_NOACTIVATE owner cannot reliably become foreground, which leaves
    // TrackPopupMenuEx's menu loop running after focus moves elsewhere.
    WNDCLASSEXW wcMenu = {0};
    wcMenu.cbSize = sizeof(WNDCLASSEXW);
    wcMenu.lpfnWndProc = PopupHostWndProc;
    wcMenu.hInstance = g_hInst;
    wcMenu.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcMenu.lpszClassName = L"F4MenuPopupHost";
    RegisterClassExW(&wcMenu);

    HWND hHost = CreateWindowExW(WS_EX_TOOLWINDOW,
        L"F4MenuPopupHost", L"", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, g_hInst, NULL);

    int selected = 0;
    if (hHost) {
        // A popup menu owner must be the foreground window so Windows will
        // cancel menu tracking when the user switches to another application.
        SetForegroundWindow(hHost);
        selected = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTBUTTON,
            pt.x, pt.y, hHost, NULL);
        PostMessageW(hHost, WM_NULL, 0, 0);
        DestroyWindow(hHost);
    }
    UnregisterClassW(L"F4MenuPopupHost", g_hInst);
    
    FreeIconCache();
    DestroyMenu(hMenu);
    
    if (selected > 0 && selected <= g_programCount) {
        ExecuteProgram(&g_programs[selected - 1], files, fileCount);
    } else if (selected >= 10000 && selected < 10000 + g_programCount) {
        ExecuteProgram(&g_programs[selected - 10000], files, fileCount);
    } else if (selected == 9002) {
        // "设置" - open config mode
        ConfigMode();
    } else if (selected == 9001) {
        // "其它程序..." - like main window "Add" button: select exe first, then show edit dialog
        OPENFILENAMEW ofn = {0};
        WCHAR exePath[MAX_PATH] = {0};
        
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFilter = L"可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
        ofn.lpstrFile = exePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"选择要添加的程序";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        
        if (GetOpenFileNameW(&ofn)) {
            // Extract name: try PE version info for .exe, fallback to filename
            WCHAR* lastDot = wcsrchr(exePath, L'.');
            if (lastDot && _wcsicmp(lastDot, L".exe") == 0) {
                WCHAR prodName[MAX_NAME_LEN] = {0};
                if (GetExeProductName(exePath, prodName, MAX_NAME_LEN) && wcslen(prodName) > 0) {
                    wcscpy_s(g_prefillName, MAX_NAME_LEN, prodName);
                } else {
                    WCHAR* lastSlash = wcsrchr(exePath, L'\\');
                    WCHAR* name = lastSlash ? lastSlash + 1 : exePath;
                    wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
                    WCHAR* dot = wcsrchr(g_prefillName, L'.');
                    if (dot) *dot = L'\0';
                }
            } else {
                WCHAR* lastSlash = wcsrchr(exePath, L'\\');
                WCHAR* name = lastSlash ? lastSlash + 1 : exePath;
                wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
                WCHAR* dot = wcsrchr(g_prefillName, L'.');
                if (dot) *dot = L'\0';
            }
            
            // Store full path for icon extraction
            wcscpy_s(g_prefillPath, MAX_PATH_LEN, exePath);
            g_prefillStart[0] = L'\0';
            
            // Pre-fill type with the file extension being opened
            WCHAR ext[MAX_TYPE_LEN] = {0};
            if (fileCount > 0) {
                WCHAR* dot = wcsrchr(files[0], L'.');
                if (dot) {
                    dot++;
                    wcscpy_s(ext, MAX_TYPE_LEN, dot);
                }
            }
            wcscpy_s(g_prefillType, MAX_TYPE_LEN, ext);
            
            // Create hidden host window for the edit dialog
            LoadSettings();
            
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = g_hInst;
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = L"F4MenuConfigLaunch";
            
            if (RegisterClassExW(&wc)) {
                HWND hwnd = CreateWindowExW(0, L"F4MenuConfigLaunch", L"F4Menu",
                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
                    NULL, NULL, g_hInst, NULL);
                
                if (hwnd) {
                    ShowEditDialog(hwnd, -1);
                    DestroyWindow(hwnd);
                    UnregisterClassW(L"F4MenuConfigLaunch", g_hInst);
                }
            }
            
            g_prefillPath[0] = L'\0';
            g_prefillName[0] = L'\0';
            g_prefillStart[0] = L'\0';
            g_prefillType[0] = L'\0';
        }
    }
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;
    
    // Enable DPI awareness (Vista+)
    {
        typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32) {
            PFN_SetProcessDPIAware pfn = (PFN_SetProcessDPIAware)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (pfn) pfn();
        }
    }
    
    // Get system DPI
    {
        HDC hScreenDC = GetDC(NULL);
        if (hScreenDC) {
            g_dpi = GetDeviceCaps(hScreenDC, LOGPIXELSY);
            ReleaseDC(NULL, hScreenDC);
        }
    }
    
    // Create UI font (Microsoft YaHei UI, scaled to DPI)
    g_hFont = CreateFontW(
        -DPI_SCALE(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
        L"Microsoft YaHei UI");
    if (!g_hFont) {
        g_hFont = CreateFontW(
            -DPI_SCALE(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
            L"Segoe UI");
    }
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    
    // Parse command line
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    // Scan for /ini parameter and collect file arguments
    WCHAR* fileArgs[256];
    int fileArgCount = 0;
    
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/ini") == 0 && i + 1 < argc) {
            wcscpy_s(g_customIniPath, MAX_PATH, argv[++i]);
        } else if (argv[i][0] != L'/') {
            fileArgs[fileArgCount++] = argv[i];
        }
    }
    
    if (fileArgCount == 0) {
        // Configuration mode
        int result = ConfigMode();
        LocalFree(argv);
        return result;
    } else {
        // Launch mode
        LaunchMode(fileArgCount, fileArgs);
        LocalFree(argv);
        return 0;
    }
}

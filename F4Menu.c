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
#define MAX_PROGRAMS 100
#define MAX_PATH_LEN 1024
#define MAX_PARAM_LEN 512
#define MAX_TYPE_LEN 1024
#define MAX_NAME_LEN 256

#define IDC_LISTVIEW 1001
#define IDC_BTN_ADD 1002
#define IDC_BTN_EDIT 1003
#define IDC_BTN_DELETE 1004
#define IDC_BTN_SAVE 1005
#define IDC_BTN_ABOUT 1006
#define IDC_BTN_EXIT 1007

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

// Program configuration structure
typedef struct {
    WCHAR name[MAX_NAME_LEN];
    WCHAR path[MAX_PATH_LEN];
    WCHAR param[MAX_PARAM_LEN];
    WCHAR start[MAX_PATH_LEN];
    WCHAR icon[MAX_PATH_LEN];
    WCHAR type[MAX_TYPE_LEN];
    int mode;      // 0=独立, 1=合并
    int window;    // 0=常规, 1=最大化, 2=最小化
    int main;      // Reserved
} ProgramConfig;

// DPI scaling
int g_dpi = 96;
#define DPI_SCALE(val) MulDiv(val, g_dpi, 96)

// Global variables
HINSTANCE g_hInst;
WCHAR g_iniPath[MAX_PATH];
ProgramConfig g_programs[MAX_PROGRAMS];
int g_programCount = 0;
HWND g_hListView = NULL;
HIMAGELIST g_hImageList = NULL;
HFONT g_hFont = NULL;

// Pre-fill data for add dialog
WCHAR g_prefillPath[MAX_PATH_LEN] = {0};
WCHAR g_prefillName[MAX_NAME_LEN] = {0};
WCHAR g_prefillStart[MAX_PATH_LEN] = {0};
WCHAR g_prefillType[MAX_TYPE_LEN] = {0};
int g_editIndex = -1;
int g_selectedIconIndex = 0;

// Settings
typedef struct {
    int winPosX;
    int winPosY;
    int winWidth;
    int winHeight;
    int columnWidths[9];
} Settings;

Settings g_settings = {0};

// Function declarations
void GetIniFilePath(WCHAR* path, DWORD size);
void LoadSettings();
void SaveSettings();
void LoadPrograms();
void SavePrograms();
void InitListView(HWND hwnd);
void PopulateListView();
void AddProgramToListView(int index);
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

// Get INI file path (same directory as executable)
void GetIniFilePath(WCHAR* path, DWORD size) {
    GetModuleFileNameW(NULL, path, size);
    WCHAR* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
    }
    wcscat_s(path, size, L"F4Menu.ini");
}

// Expand environment variables
void ExpandEnvStrings(const WCHAR* src, WCHAR* dst, DWORD dstSize) {
    ExpandEnvironmentStringsW(src, dst, dstSize);
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

// Load settings from INI
void LoadSettings() {
    GetIniFilePath(g_iniPath, MAX_PATH);
    
    DWORD winPos = GetPrivateProfileIntW(L"Settings", L"WinPos", 15598003, g_iniPath);
    DWORD winSize = GetPrivateProfileIntW(L"Settings", L"WinSize", 25428634, g_iniPath);
    
    g_settings.winPosX = LOWORD(winPos);
    g_settings.winPosY = HIWORD(winPos);
    g_settings.winWidth = LOWORD(winSize);
    g_settings.winHeight = HIWORD(winSize);
    
    // Default window size if invalid
    if (g_settings.winWidth < DPI_SCALE(400)) g_settings.winWidth = DPI_SCALE(800);
    if (g_settings.winHeight < DPI_SCALE(300)) g_settings.winHeight = DPI_SCALE(600);
    
    // Load column widths
    WCHAR columnStr[512] = {0};
    GetPrivateProfileStringW(L"Settings", L"Column", L"164,310,67,234,63,38,50,76,157,", 
                             columnStr, 512, g_iniPath);
    
    int idx = 0;
    WCHAR* context = NULL;
    WCHAR* token = wcstok(columnStr, L",", &context);
    while (token && idx < 9) {
        g_settings.columnWidths[idx++] = _wtoi(token);
        token = wcstok(NULL, L",", &context);
    }
    
    // Default column widths
    int defaults[] = {24, 150, 200, 100, 150, 60, 60, 60, 150};
    for (int i = 0; i < 9; i++) {
        if (g_settings.columnWidths[i] <= 0) {
            g_settings.columnWidths[i] = defaults[i];
        }
    }
}

// Save settings to INI
void SaveSettings() {
    WCHAR buffer[64];
    
    DWORD winPos = MAKELONG(g_settings.winPosX, g_settings.winPosY);
    DWORD winSize = MAKELONG(g_settings.winWidth, g_settings.winHeight);
    
    swprintf(buffer, 64, L"%u", winPos);
    WritePrivateProfileStringW(L"Settings", L"WinPos", buffer, g_iniPath);
    
    swprintf(buffer, 64, L"%u", winSize);
    WritePrivateProfileStringW(L"Settings", L"WinSize", buffer, g_iniPath);
    
    // Save column widths
    WCHAR columnStr[512] = {0};
    WCHAR temp[32];
    for (int i = 0; i < 9; i++) {
        int width = (int)SendMessageW(g_hListView, LVM_GETCOLUMNWIDTH, i, 0);
        if (width > 0) {
            swprintf(temp, 32, L"%d,", width);
            wcscat_s(columnStr, 512, temp);
        }
    }
    WritePrivateProfileStringW(L"Settings", L"Column", columnStr, g_iniPath);
}

// Load programs from INI
void LoadPrograms() {
    GetIniFilePath(g_iniPath, MAX_PATH);
    
    g_programCount = GetPrivateProfileIntW(L"General", L"Count", 0, g_iniPath);
    if (g_programCount > MAX_PROGRAMS) g_programCount = MAX_PROGRAMS;
    
    for (int i = 0; i < g_programCount; i++) {
        WCHAR section[32];
        swprintf(section, 32, L"Program%d", i);
        
        GetPrivateProfileStringW(section, L"Name", L"", g_programs[i].name, MAX_NAME_LEN, g_iniPath);
        GetPrivateProfileStringW(section, L"Path", L"", g_programs[i].path, MAX_PATH_LEN, g_iniPath);
        GetPrivateProfileStringW(section, L"Param", L"", g_programs[i].param, MAX_PARAM_LEN, g_iniPath);
        GetPrivateProfileStringW(section, L"Start", L"", g_programs[i].start, MAX_PATH_LEN, g_iniPath);
        GetPrivateProfileStringW(section, L"Icon", L"", g_programs[i].icon, MAX_PATH_LEN, g_iniPath);
        GetPrivateProfileStringW(section, L"Type", L"", g_programs[i].type, MAX_TYPE_LEN, g_iniPath);
        
        g_programs[i].mode = GetPrivateProfileIntW(section, L"Mode", 0, g_iniPath);
        g_programs[i].window = GetPrivateProfileIntW(section, L"Window", 0, g_iniPath);
        g_programs[i].main = GetPrivateProfileIntW(section, L"Main", 0, g_iniPath);
    }
}

// Save programs to INI
void SavePrograms() {
    WCHAR buffer[64];
    
    // Update count
    swprintf(buffer, 64, L"%d", g_programCount);
    WritePrivateProfileStringW(L"General", L"Count", buffer, g_iniPath);
    
    // Save each program
    for (int i = 0; i < g_programCount; i++) {
        WCHAR section[32];
        swprintf(section, 32, L"Program%d", i);
        
        WritePrivateProfileStringW(section, L"Name", g_programs[i].name, g_iniPath);
        WritePrivateProfileStringW(section, L"Path", g_programs[i].path, g_iniPath);
        WritePrivateProfileStringW(section, L"Param", g_programs[i].param, g_iniPath);
        WritePrivateProfileStringW(section, L"Start", g_programs[i].start, g_iniPath);
        WritePrivateProfileStringW(section, L"Icon", g_programs[i].icon, g_iniPath);
        WritePrivateProfileStringW(section, L"Type", g_programs[i].type, g_iniPath);
        
        swprintf(buffer, 64, L"%d", g_programs[i].mode);
        WritePrivateProfileStringW(section, L"Mode", buffer, g_iniPath);
        
        swprintf(buffer, 64, L"%d", g_programs[i].window);
        WritePrivateProfileStringW(section, L"Window", buffer, g_iniPath);
        
        swprintf(buffer, 64, L"%d", g_programs[i].main);
        WritePrivateProfileStringW(section, L"Main", buffer, g_iniPath);
    }
    
    // Remove old program sections
    for (int i = g_programCount; i < MAX_PROGRAMS; i++) {
        WCHAR section[32];
        swprintf(section, 32, L"Program%d", i);
        WritePrivateProfileStringW(section, NULL, NULL, g_iniPath);
    }
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
                              L"打开方式", L"窗口模式", L"主窗口", L"关联扩展名"};
    
    for (int i = 0; i < 9; i++) {
        col.pszText = (LPWSTR)headers[i];
        col.cx = g_settings.columnWidths[i];
        SendMessageW(g_hListView, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
    }
}

// Add program to ListView
void AddProgramToListView(int index) {
    if (index < 0 || index >= g_programCount) return;
    
    ProgramConfig* prog = &g_programs[index];
    
    // Add icon to image list
    HICON hIcon = LoadIconFromPath(prog->icon);
    int imageIndex = ImageList_AddIcon(g_hImageList, hIcon);
    DestroyIcon(hIcon);
    
    // Insert item
    LVITEMW item = {0};
    item.mask = LVIF_TEXT | LVIF_IMAGE;
    item.iItem = index;
    item.iImage = imageIndex;
    item.pszText = L"";
    SendMessageW(g_hListView, LVM_INSERTITEMW, 0, (LPARAM)&item);
    
    // Set subitems
    ListView_SetItemText(g_hListView, index, 1, prog->name);
    ListView_SetItemText(g_hListView, index, 2, prog->path);
    ListView_SetItemText(g_hListView, index, 3, prog->param);
    ListView_SetItemText(g_hListView, index, 4, prog->start);
    
    WCHAR* modeText = prog->mode == 0 ? L"独立" : L"合并";
    ListView_SetItemText(g_hListView, index, 5, modeText);
    
    WCHAR* windowText = prog->window == 0 ? L"常规" : (prog->window == 1 ? L"最大化" : L"最小化");
    ListView_SetItemText(g_hListView, index, 6, windowText);
    
    WCHAR mainText[16];
    swprintf(mainText, 16, L"%d", prog->main);
    ListView_SetItemText(g_hListView, index, 7, mainText);
    
    ListView_SetItemText(g_hListView, index, 8, prog->type);
}

// Populate ListView with all programs
void PopulateListView() {
    SendMessageW(g_hListView, LVM_DELETEALLITEMS, 0, 0);
    ImageList_RemoveAll(g_hImageList);
    
    for (int i = 0; i < g_programCount; i++) {
        AddProgramToListView(i);
    }
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
                    prog.main = 0;
                    
                    if (wcslen(prog.name) == 0 || wcslen(prog.path) == 0) {
                        MessageBoxW(hwnd, L"名称和路径不能为空！", L"错误", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    
                    if (g_editIndex >= 0 && g_editIndex < g_programCount) {
                        g_programs[g_editIndex] = prog;
                    } else {
                        if (g_programCount < MAX_PROGRAMS) {
                            g_programs[g_programCount++] = prog;
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
    
    int dlgW = DPI_SCALE(500);
    int dlgH = DPI_SCALE(520);
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",
        L"编辑程序",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, dlgW, dlgH,
        parent, NULL, g_hInst, NULL
    );
    
    if (!hwnd) return;
    
    int y = DPI_SCALE(10);
    int lx = DPI_SCALE(10);
    int labelWidth = DPI_SCALE(80);
    int editWidth = DPI_SCALE(350);
    int spacing = DPI_SCALE(30);
    int editH = DPI_SCALE(22);
    int btnH = DPI_SCALE(25);
    int browseW = DPI_SCALE(60);
    int comboW = DPI_SCALE(150);
    int comboH = DPI_SCALE(100);
    int iconListH = DPI_SCALE(68);
    
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
    
    // 扩展名
    CreateWindowW(L"STATIC", L"扩展名:", WS_CHILD | WS_VISIBLE, lx, y, labelWidth, editH, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        lx + labelWidth, y, editWidth, editH, hwnd, (HMENU)IDD_EDIT_TYPE, g_hInst, NULL);
    y += spacing + DPI_SCALE(10);
    
    // 按钮
    CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        lx + labelWidth + editWidth - DPI_SCALE(160), y, DPI_SCALE(70), btnH, hwnd, (HMENU)IDD_BTN_OK, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        lx + labelWidth + editWidth - DPI_SCALE(80), y, DPI_SCALE(70), btnH, hwnd, (HMENU)IDD_BTN_CANCEL, g_hInst, NULL);
    
    SetDialogFont(hwnd, g_hFont);
    
    SetWindowSubclass(hwnd, EditDialogSubclassProc, 0, 0);
    
    if (index >= 0 && index < g_programCount) {
        ProgramConfig* prog = &g_programs[index];
        SetDlgItemTextW(hwnd, IDD_EDIT_NAME, prog->name);
        SetDlgItemTextW(hwnd, IDD_EDIT_PATH, prog->path);
        SetDlgItemTextW(hwnd, IDD_EDIT_PARAM, prog->param);
        SetDlgItemTextW(hwnd, IDD_EDIT_START, prog->start);
        SetDlgItemTextW(hwnd, IDD_EDIT_TYPE, prog->type);
        SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, prog->mode, 0);
        SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, prog->window, 0);
        
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
    
    // Delete from end to beginning to maintain indices
    int index = -1;
    while ((index = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, index, LVNI_SELECTED)) != -1) {
        // Mark for deletion by moving to end
        if (index < g_programCount - 1) {
            ProgramConfig temp = g_programs[index];
            for (int i = index; i < g_programCount - 1; i++) {
                g_programs[i] = g_programs[i + 1];
            }
            g_programs[g_programCount - 1] = temp;
        }
        g_programCount--;
        index--; // Adjust for shift
    }
    
    PopulateListView();
}

// Show about dialog
void ShowAboutDialog(HWND parent) {
    MessageBoxW(parent, 
        L"F4Menu v1.0\n\n"
        L"Windows 文件启动器和配置工具\n\n"
        L"使用纯 Win32 API 开发\n"
        L"兼容 Windows XP 到 Windows 11\n\n"
        L"© 2025",
        L"关于 F4Menu",
        MB_OK | MB_ICONINFORMATION);
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
            
            // Create buttons
            int btnY = DPI_SCALE(500);
            int btnX = DPI_SCALE(10);
            int btnWidth = DPI_SCALE(80);
            int btnHeight = DPI_SCALE(30);
            int btnSpacing = DPI_SCALE(90);
            
            CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_ADD, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_EDIT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_DELETE, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_SAVE, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_ABOUT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"退出", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, btnHeight, hwnd, (HMENU)IDC_BTN_EXIT, g_hInst, NULL);
            
            SetDialogFont(hwnd, g_hFont);
            
            return 0;
        }
        
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            
            // Resize ListView
            SetWindowPos(g_hListView, NULL, DPI_SCALE(10), DPI_SCALE(10), width - DPI_SCALE(20), height - DPI_SCALE(60), SWP_NOZORDER);
            
            // Reposition buttons
            int btnY = height - DPI_SCALE(40);
            int btnX = DPI_SCALE(10);
            int btnSpacing = DPI_SCALE(90);
            
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_ADD), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            btnX += btnSpacing;
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_EDIT), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            btnX += btnSpacing;
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_DELETE), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            btnX += btnSpacing;
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_SAVE), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            btnX += btnSpacing;
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_ABOUT), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            btnX += btnSpacing;
            SetWindowPos(GetDlgItem(hwnd, IDC_BTN_EXIT), NULL, btnX, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            
            return 0;
        }
        
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BTN_ADD: {
                    // Show file open dialog first
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
                        // Extract name: try PE version info for .exe, fallback to filename
                        WCHAR* lastDot = wcsrchr(fileName, L'.');
                        if (lastDot && _wcsicmp(lastDot, L".exe") == 0) {
                            WCHAR prodName[MAX_NAME_LEN] = {0};
                            if (GetExeProductName(fileName, prodName, MAX_NAME_LEN) && wcslen(prodName) > 0) {
                                wcscpy_s(g_prefillName, MAX_NAME_LEN, prodName);
                            } else {
                                // Fallback to filename without extension
                                WCHAR* lastSlash = wcsrchr(fileName, L'\\');
                                WCHAR* name = lastSlash ? lastSlash + 1 : fileName;
                                wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
                                WCHAR* dot = wcsrchr(g_prefillName, L'.');
                                if (dot) *dot = L'\0';
                            }
                        } else {
                            WCHAR* lastSlash = wcsrchr(fileName, L'\\');
                            WCHAR* name = lastSlash ? lastSlash + 1 : fileName;
                            wcscpy_s(g_prefillName, MAX_NAME_LEN, name);
                            WCHAR* dot = wcsrchr(g_prefillName, L'.');
                            if (dot) *dot = L'\0';
                        }
                        
                        // Store full path
                        wcscpy_s(g_prefillPath, MAX_PATH_LEN, fileName);
                        
                        ShowEditDialog(hwnd, -1);
                        
                        // Clear pre-fill data
                        g_prefillPath[0] = L'\0';
                        g_prefillName[0] = L'\0';
                        g_prefillType[0] = L'\0';
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
                
                case IDC_BTN_SAVE:
                    SavePrograms();
                    MessageBoxW(hwnd, L"配置已保存！", L"提示", MB_OK | MB_ICONINFORMATION);
                    return 0;
                
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
            if (nmhdr->idFrom == IDC_LISTVIEW && nmhdr->code == NM_DBLCLK) {
                int selected = (int)SendMessageW(g_hListView, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                if (selected >= 0) {
                    ShowEditDialog(hwnd, selected);
                }
            }
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
    HWND hwnd = CreateWindowExW(
        0,
        L"F4MenuConfig",
        L"F4Menu - 配置",
        WS_OVERLAPPEDWINDOW,
        g_settings.winPosX, g_settings.winPosY,
        g_settings.winWidth, g_settings.winHeight,
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
    
    ExpandEnvStrings(prog->path, expandedPath, MAX_PATH_LEN);
    ExpandEnvStrings(prog->start, expandedStart, MAX_PATH_LEN);
    
    // Determine window state
    int nShowCmd = SW_SHOWNORMAL;
    if (prog->window == 1) nShowCmd = SW_SHOWMAXIMIZED;
    else if (prog->window == 2) nShowCmd = SW_SHOWMINIMIZED;
    
    if (prog->mode == 0) {
        // Independent mode - launch for each file
        for (int i = 0; i < fileCount; i++) {
            WCHAR cmdLine[MAX_PATH_LEN * 2];
            swprintf(cmdLine, MAX_PATH_LEN * 2, L"\"%s\" %s \"%s\"", 
                expandedPath, prog->param, files[i]);
            
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
        swprintf(cmdLine, MAX_PATH_LEN * 4, L"\"%s\" %s", expandedPath, prog->param);
        
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

// Launch mode
void LaunchMode(int argc, WCHAR** argv) {
    LoadPrograms();
    
    if (argc < 2) return;
    
    // Extract file extensions
    WCHAR** files = argv + 1;
    int fileCount = argc - 1;
    
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
        
        HICON hIcon = LoadIconFromPath(g_programs[progIdx].icon);
        if (hIcon) {
            mii.hbmpItem = IconToBitmap(hIcon, menuIconSize);
            DestroyIcon(hIcon);
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
        
        // Check if program path exists
        WCHAR expandedPath[MAX_PATH_LEN];
        ExpandEnvStrings(g_programs[i].path, expandedPath, MAX_PATH_LEN);
        if (GetFileAttributesW(expandedPath) == INVALID_FILE_ATTRIBUTES) {
            mii.fState = MFS_DISABLED | MFS_GRAYED;
        }
        
        HICON hIcon = LoadIconFromPath(g_programs[i].icon);
        if (hIcon) {
            mii.hbmpItem = IconToBitmap(hIcon, menuIconSize);
            DestroyIcon(hIcon);
        }
        
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
    
    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);
    
    // Create hidden message-only window as menu owner (fixes popup not appearing)
    WNDCLASSEXW wcMenu = {0};
    wcMenu.cbSize = sizeof(WNDCLASSEXW);
    wcMenu.lpfnWndProc = DefWindowProcW;
    wcMenu.hInstance = g_hInst;
    wcMenu.lpszClassName = L"F4MenuPopupHost";
    RegisterClassExW(&wcMenu);
    
    HWND hHost = CreateWindowExW(0, L"F4MenuPopupHost", L"", WS_OVERLAPPED,
        0, 0, 0, 0, HWND_MESSAGE, NULL, g_hInst, NULL);
    
    // Show menu
    SetForegroundWindow(hHost);
    int selected = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTBUTTON, 
        pt.x, pt.y, hHost, NULL);
    PostMessage(hHost, WM_NULL, 0, 0);
    
    DestroyWindow(hHost);
    UnregisterClassW(L"F4MenuPopupHost", g_hInst);
    
    // Free menu item bitmaps
    for (int i = 0; i < matchCount; i++) {
        MENUITEMINFOW mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_BITMAP;
        if (GetMenuItemInfoW(hMenu, matchedPrograms[i] + 1, FALSE, &mii) && mii.hbmpItem) {
            DeleteObject(mii.hbmpItem);
        }
    }
    for (int i = 0; i < g_programCount; i++) {
        MENUITEMINFOW mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_BITMAP;
        if (GetMenuItemInfoW(hSubMenuAll, 10000 + i, FALSE, &mii) && mii.hbmpItem) {
            DeleteObject(mii.hbmpItem);
        }
    }
    DestroyMenu(hMenu);
    
    if (selected > 0 && selected <= g_programCount) {
        ExecuteProgram(&g_programs[selected - 1], files, fileCount);
    } else if (selected >= 10000 && selected < 10000 + g_programCount) {
        ExecuteProgram(&g_programs[selected - 10000], files, fileCount);
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
    
    if (argc <= 1) {
        // Configuration mode
        int result = ConfigMode();
        LocalFree(argv);
        return result;
    } else {
        // Launch mode
        LaunchMode(argc, argv);
        LocalFree(argv);
        return 0;
    }
}

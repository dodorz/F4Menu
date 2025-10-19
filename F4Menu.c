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
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

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
#define IDD_EDIT_ICON 2005
#define IDD_EDIT_TYPE 2006
#define IDD_COMBO_MODE 2007
#define IDD_COMBO_WINDOW 2008
#define IDD_BTN_BROWSE_PATH 2009
#define IDD_BTN_BROWSE_ICON 2010
#define IDD_BTN_OK 2011
#define IDD_BTN_CANCEL 2012

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

// Global variables
HINSTANCE g_hInst;
WCHAR g_iniPath[MAX_PATH];
ProgramConfig g_programs[MAX_PROGRAMS];
int g_programCount = 0;
HWND g_hListView = NULL;
HIMAGELIST g_hImageList = NULL;

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
INT_PTR CALLBACK EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowEditDialog(HWND parent, int index);
void DeleteSelectedPrograms(HWND hwnd);
void ShowAboutDialog(HWND parent);
void LaunchMode(int argc, WCHAR** argv);
BOOL MatchExtension(const WCHAR* fileExt, const WCHAR* typeList);
void ExecuteProgram(ProgramConfig* prog, WCHAR** files, int fileCount);
HICON LoadIconFromPath(const WCHAR* iconPath);
void ExpandEnvStrings(const WCHAR* src, WCHAR* dst, DWORD dstSize);

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
    if (g_settings.winWidth < 400) g_settings.winWidth = 800;
    if (g_settings.winHeight < 300) g_settings.winHeight = 600;
    
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
    
    // Set extended styles
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    SendMessageW(g_hListView, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, exStyle);
    
    // Create image list for icons
    g_hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 10, 10);
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

// Edit dialog procedure
INT_PTR CALLBACK EditDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int* pIndex = NULL;
    
    switch (msg) {
        case WM_INITDIALOG: {
            pIndex = (int*)lParam;
            
            // Center dialog
            RECT rc, rcOwner;
            GetWindowRect(hwnd, &rc);
            GetWindowRect(GetParent(hwnd), &rcOwner);
            SetWindowPos(hwnd, NULL,
                rcOwner.left + (rcOwner.right - rcOwner.left - (rc.right - rc.left)) / 2,
                rcOwner.top + (rcOwner.bottom - rcOwner.top - (rc.bottom - rc.top)) / 2,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
            
            // Populate fields if editing existing program
            if (*pIndex >= 0 && *pIndex < g_programCount) {
                ProgramConfig* prog = &g_programs[*pIndex];
                SetDlgItemTextW(hwnd, IDD_EDIT_NAME, prog->name);
                SetDlgItemTextW(hwnd, IDD_EDIT_PATH, prog->path);
                SetDlgItemTextW(hwnd, IDD_EDIT_PARAM, prog->param);
                SetDlgItemTextW(hwnd, IDD_EDIT_START, prog->start);
                SetDlgItemTextW(hwnd, IDD_EDIT_ICON, prog->icon);
                SetDlgItemTextW(hwnd, IDD_EDIT_TYPE, prog->type);
                
                SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, prog->mode, 0);
                SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, prog->window, 0);
            } else {
                SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_SETCURSEL, 0, 0);
                SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_SETCURSEL, 0, 0);
            }
            
            return TRUE;
        }
        
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
                        
                        // Auto-fill icon if empty
                        WCHAR iconPath[MAX_PATH_LEN];
                        GetDlgItemTextW(hwnd, IDD_EDIT_ICON, iconPath, MAX_PATH_LEN);
                        if (wcslen(iconPath) == 0) {
                            wcscat_s(fileName, MAX_PATH, L",0");
                            SetDlgItemTextW(hwnd, IDD_EDIT_ICON, fileName);
                        }
                    }
                    return TRUE;
                }
                
                case IDD_BTN_BROWSE_ICON: {
                    OPENFILENAMEW ofn = {0};
                    WCHAR fileName[MAX_PATH] = {0};
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"图标文件 (*.exe;*.dll;*.ico)\0*.exe;*.dll;*.ico\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        wcscat_s(fileName, MAX_PATH, L",0");
                        SetDlgItemTextW(hwnd, IDD_EDIT_ICON, fileName);
                    }
                    return TRUE;
                }
                
                case IDD_BTN_OK: {
                    ProgramConfig prog = {0};
                    
                    GetDlgItemTextW(hwnd, IDD_EDIT_NAME, prog.name, MAX_NAME_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_PATH, prog.path, MAX_PATH_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_PARAM, prog.param, MAX_PARAM_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_START, prog.start, MAX_PATH_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_ICON, prog.icon, MAX_PATH_LEN);
                    GetDlgItemTextW(hwnd, IDD_EDIT_TYPE, prog.type, MAX_TYPE_LEN);
                    
                    prog.mode = (int)SendDlgItemMessageW(hwnd, IDD_COMBO_MODE, CB_GETCURSEL, 0, 0);
                    prog.window = (int)SendDlgItemMessageW(hwnd, IDD_COMBO_WINDOW, CB_GETCURSEL, 0, 0);
                    prog.main = 0;
                    
                    // Validate
                    if (wcslen(prog.name) == 0 || wcslen(prog.path) == 0) {
                        MessageBoxW(hwnd, L"名称和路径不能为空！", L"错误", MB_OK | MB_ICONERROR);
                        return TRUE;
                    }
                    
                    // Save program
                    if (*pIndex >= 0 && *pIndex < g_programCount) {
                        // Edit existing
                        g_programs[*pIndex] = prog;
                    } else {
                        // Add new
                        if (g_programCount < MAX_PROGRAMS) {
                            g_programs[g_programCount++] = prog;
                        } else {
                            MessageBoxW(hwnd, L"已达到最大程序数量限制！", L"错误", MB_OK | MB_ICONERROR);
                            return TRUE;
                        }
                    }
                    
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                
                case IDD_BTN_CANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        }
        
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    
    return FALSE;
}

// Create edit dialog
HWND CreateEditDialog(HWND parent) {
    // Create dialog window
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",  // Dialog class
        L"编辑程序",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
        parent, NULL, g_hInst, NULL
    );
    
    if (!hwnd) return NULL;
    
    // Create controls
    int y = 10;
    int labelWidth = 80;
    int editWidth = 350;
    int spacing = 35;
    
    // Name
    CreateWindowW(L"STATIC", L"名称:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth, 22, hwnd, (HMENU)IDD_EDIT_NAME, g_hInst, NULL);
    y += spacing;
    
    // Path
    CreateWindowW(L"STATIC", L"路径:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth - 70, 22, hwnd, (HMENU)IDD_EDIT_PATH, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        labelWidth + editWidth - 60, y, 60, 22, hwnd, (HMENU)IDD_BTN_BROWSE_PATH, g_hInst, NULL);
    y += spacing;
    
    // Param
    CreateWindowW(L"STATIC", L"参数:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth, 22, hwnd, (HMENU)IDD_EDIT_PARAM, g_hInst, NULL);
    y += spacing;
    
    // Start
    CreateWindowW(L"STATIC", L"启动路径:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth, 22, hwnd, (HMENU)IDD_EDIT_START, g_hInst, NULL);
    y += spacing;
    
    // Icon
    CreateWindowW(L"STATIC", L"图标:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth - 70, 22, hwnd, (HMENU)IDD_EDIT_ICON, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        labelWidth + editWidth - 60, y, 60, 22, hwnd, (HMENU)IDD_BTN_BROWSE_ICON, g_hInst, NULL);
    y += spacing;
    
    // Type
    CreateWindowW(L"STATIC", L"扩展名:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        labelWidth + 10, y, editWidth, 22, hwnd, (HMENU)IDD_EDIT_TYPE, g_hInst, NULL);
    y += spacing;
    
    // Mode
    CreateWindowW(L"STATIC", L"打开方式:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    HWND hComboMode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        labelWidth + 10, y, 150, 100, hwnd, (HMENU)IDD_COMBO_MODE, g_hInst, NULL);
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"独立");
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"合并");
    y += spacing;
    
    // Window
    CreateWindowW(L"STATIC", L"窗口模式:", WS_CHILD | WS_VISIBLE, 10, y, labelWidth, 20, hwnd, NULL, g_hInst, NULL);
    HWND hComboWindow = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        labelWidth + 10, y, 150, 100, hwnd, (HMENU)IDD_COMBO_WINDOW, g_hInst, NULL);
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"常规");
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"最大化");
    SendMessageW(hComboWindow, CB_ADDSTRING, 0, (LPARAM)L"最小化");
    y += spacing + 10;
    
    // Buttons
    CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        labelWidth + editWidth - 160, y, 70, 25, hwnd, (HMENU)IDD_BTN_OK, g_hInst, NULL);
    CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        labelWidth + editWidth - 80, y, 70, 25, hwnd, (HMENU)IDD_BTN_CANCEL, g_hInst, NULL);
    
    return hwnd;
}

// Show edit dialog
void ShowEditDialog(HWND parent, int index) {
    HWND hwnd = CreateEditDialog(parent);
    if (!hwnd) return;
    
    // Set dialog data
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&index);
    
    // Initialize dialog
    SendMessageW(hwnd, WM_INITDIALOG, 0, (LPARAM)&index);
    
    // Show dialog
    ShowWindow(hwnd, SW_SHOW);
    
    // Message loop
    MSG msg;
    BOOL dialogActive = TRUE;
    
    while (dialogActive && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) {
            if (msg.message == WM_COMMAND) {
                EditDialogProc(hwnd, msg.message, msg.wParam, msg.lParam);
                
                if (LOWORD(msg.wParam) == IDD_BTN_OK || LOWORD(msg.wParam) == IDD_BTN_CANCEL) {
                    dialogActive = FALSE;
                }
            } else if (msg.message == WM_CLOSE) {
                dialogActive = FALSE;
            }
        }
        
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(hwnd);
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
                10, 10, 760, 480,
                hwnd, (HMENU)IDC_LISTVIEW, g_hInst, NULL
            );
            
            InitListView(hwnd);
            PopulateListView();
            
            // Create buttons
            int btnY = 500;
            int btnX = 10;
            int btnWidth = 80;
            int btnSpacing = 90;
            
            CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_ADD, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_EDIT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_DELETE, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_SAVE, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"关于", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_ABOUT, g_hInst, NULL);
            btnX += btnSpacing;
            
            CreateWindowW(L"BUTTON", L"退出", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                btnX, btnY, btnWidth, 30, hwnd, (HMENU)IDC_BTN_EXIT, g_hInst, NULL);
            
            return 0;
        }
        
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            
            // Resize ListView
            SetWindowPos(g_hListView, NULL, 10, 10, width - 20, height - 60, SWP_NOZORDER);
            
            // Reposition buttons
            int btnY = height - 40;
            int btnX = 10;
            int btnSpacing = 90;
            
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
                case IDC_BTN_ADD:
                    ShowEditDialog(hwnd, -1);
                    return 0;
                
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
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
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
    
    for (int i = 0; i < matchCount; i++) {
        int progIdx = matchedPrograms[i];
        
        MENUITEMINFOW mii = {0};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
        mii.wID = progIdx + 1;
        mii.dwTypeData = g_programs[progIdx].name;
        
        // Load icon as bitmap (simplified - icons in menus require HBITMAP)
        HICON hIcon = LoadIconFromPath(g_programs[progIdx].icon);
        if (hIcon) {
            // Note: Converting HICON to HBITMAP is complex, skipping for now
            DestroyIcon(hIcon);
        }
        
        InsertMenuItemW(hMenu, i, TRUE, &mii);
    }
    
    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);
    
    // Show menu
    SetForegroundWindow(GetDesktopWindow());
    int selected = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTBUTTON, 
        pt.x, pt.y, GetDesktopWindow(), NULL);
    
    DestroyMenu(hMenu);
    
    if (selected > 0) {
        ExecuteProgram(&g_programs[selected - 1], files, fileCount);
    }
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;
    
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

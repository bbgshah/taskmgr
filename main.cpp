// ============================================================================
//  Taskmgr - Task Manager for Windows 10/11            Built by Subhajit Maji :)
//  Single-file Win32 + GDI+ app.  Mica, tray mode, live performance graphs.
// ============================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace Gdiplus;

#define WM_TRAY     (WM_APP + 1)
#define WM_SHOWMAIN (WM_APP + 2)
enum { IDM_OPEN = 1001, IDM_EXIT, IDM_ACT, IDM_LOC, IDM_PRIO = 1200 };
enum { H_TAB = 100, H_PERF = 200, H_ACT = 300, H_SET = 400 };
enum { M_CPU, M_MEM, M_DISK, M_NET, M_GPU, M_N };
enum { S_MICA, S_THEME, S_CLOSE, S_STARTUP, S_REFRESH, S_PRIO, S_TRAYHOVER, S_TRAYICON, S_TOPMOST, S_CONFIRM, S_COUNT };

static const wchar_t* kMName[M_N] = { L"CPU", L"Memory", L"Disk", L"Network", L"GPU" };
static const COLORREF kCol[M_N] = { RGB(17,125,187), RGB(139,18,174), RGB(76,162,10), RGB(167,79,1), RGB(0,150,160) };
static const wchar_t* kCfgName[S_COUNT] = { L"Mica", L"Theme", L"CloseToTray", L"StartWithWindows", L"Refresh", L"Priority", L"TrayHover", L"TrayIcon", L"TopMost", L"Confirm" };
static const int kCfgDef[S_COUNT] = { 1, 0, 1, 0, 0, 1, 1, 0, 0, 1 };
static const DWORD kPrioCls[3] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS };
static const COLORREF ACCENT = RGB(0, 120, 212);
static const int HN = 60;   // history samples

// ------------------------------------------------------------------ globals
static HINSTANCE g_hInst; static HWND g_hTray, g_hMain, g_hList; static HFONT g_font;
static UINT g_wmTaskbar; static bool g_quitting, g_dark, g_micaOn, g_gdipOn;
static ULONG_PTR g_gdipTok; static int g_dpi = 96, g_cfg[S_COUNT];
static int g_tab = 1, g_perfSel = 0; static float g_scroll = 0, g_scrollMax = 0;
static double g_hist[M_N][HN], g_val[M_N];
static double g_netRecv, g_netSend, g_diskBps, g_gpuMem; static int g_netUp;
static MEMORYSTATUSEX g_ms; static PERFORMANCE_INFORMATION g_pi;
static ULONGLONG g_prevTot, g_prevIdle, g_prevIn, g_prevOut, g_netTick, g_lastWall;
static DWORD g_ncpu = 1, g_cpuMHz; static std::wstring g_cpuName, g_gpuName;
static bool g_timerOn;

static int s(int v) { return MulDiv(v, g_dpi, 96); }
static double clampd(double v, double lo = 0, double hi = 100) { return v < lo ? lo : (v > hi ? hi : v); }
static ULONGLONG ft2u(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static std::wstring F(const wchar_t* f, ...) { wchar_t b[320]; va_list a; va_start(a, f); _vsnwprintf(b, 319, f, a); va_end(a); b[319] = 0; return b; }
static std::wstring B(double b) { const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" }; int i = 0; while (b >= 1024 && i < 4) { b /= 1024; i++; } return F(i < 2 ? L"%.0f %ls" : L"%.1f %ls", b, u[i]); }

// ------------------------------------------------------------------ settings
static void loadCfg() {
    for (int i = 0; i < S_COUNT; i++) {
        DWORD v = 0, sz = 4;
        g_cfg[i] = (RegGetValueW(HKEY_CURRENT_USER, L"Software\\SubhajitMaji\\Taskmgr", kCfgName[i], RRF_RT_REG_DWORD, 0, &v, &sz) == 0) ? (int)v : kCfgDef[i];
    }
}
static void saveCfg() {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SubhajitMaji\\Taskmgr", 0, 0, 0, KEY_SET_VALUE, 0, &k, 0)) return;
    for (int i = 0; i < S_COUNT; i++) { DWORD v = g_cfg[i]; RegSetValueExW(k, kCfgName[i], 0, REG_DWORD, (BYTE*)&v, 4); }
    RegCloseKey(k);
}
static void startupReg(bool on) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k)) return;
    if (on) { wchar_t p[MAX_PATH]; GetModuleFileNameW(0, p, MAX_PATH); std::wstring c = L"\"" + std::wstring(p) + L"\" /tray"; RegSetValueExW(k, L"Taskmgr", 0, REG_SZ, (BYTE*)c.c_str(), (DWORD)((c.size() + 1) * 2)); }
    else RegDeleteValueW(k, L"Taskmgr");
    RegCloseKey(k);
}
static void computeDark() {
    if (g_cfg[S_THEME] == 1) { g_dark = false; return; }
    if (g_cfg[S_THEME] == 2) { g_dark = true; return; }
    DWORD v = 1, sz = 4; RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &sz);
    g_dark = (v == 0);
}

// ------------------------------------------------------------------ PDH (GPU / disk)
static PDH_HQUERY g_gq, g_fq; static PDH_HCOUNTER g_cGpu, g_cDisk, g_cDiskB, g_cGpuMem; static bool g_gpuTried, g_gpuOk;
static void pdhArray(PDH_HCOUNTER c, std::map<std::wstring, double>* by, double* tot) {
    DWORD sz = 0, cnt = 0;
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &cnt, NULL) != (PDH_STATUS)PDH_MORE_DATA) return;
    std::vector<BYTE> buf(sz); auto* it = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE, &sz, &cnt, it) != 0) return;
    for (DWORD i = 0; i < cnt; i++) {
        if (it[i].FmtValue.CStatus != 0 && it[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
        double v = it[i].FmtValue.doubleValue; if (tot) *tot += v;
        if (by) { std::wstring n = it[i].szName; size_t p = n.find(L"engtype_"); (*by)[p == std::wstring::npos ? L"" : n.substr(p + 8)] += v; }
    }
}
static void gpuOpen() {
    if (g_gq || g_gpuTried) return; g_gpuTried = true;
    if (PdhOpenQueryW(0, 0, &g_gq)) { g_gq = 0; return; }
    if (PdhAddEnglishCounterW(g_gq, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_cGpu)) { PdhCloseQuery(g_gq); g_gq = 0; return; }
    g_gpuOk = true; PdhCollectQueryData(g_gq);
}
static void gpuClose() { if (g_gq) { PdhCloseQuery(g_gq); g_gq = 0; } g_gpuTried = false; g_gpuOk = false; }
static double readGpu() {
    gpuOpen(); if (!g_gq) return 0;
    PdhCollectQueryData(g_gq);
    std::map<std::wstring, double> m; pdhArray(g_cGpu, &m, nullptr);
    double best = 0; for (auto& p : m) if (p.second > best) best = p.second;
    return clampd(best);
}
static void fullOpen() {
    if (g_fq) return; if (PdhOpenQueryW(0, 0, &g_fq)) { g_fq = 0; return; }
    PdhAddEnglishCounterW(g_fq, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_cDisk);
    PdhAddEnglishCounterW(g_fq, L"\\PhysicalDisk(_Total)\\Disk Bytes/sec", 0, &g_cDiskB);
    PdhAddEnglishCounterW(g_fq, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_cGpuMem);
    PdhCollectQueryData(g_fq);
}
static void fullClose() { if (g_fq) { PdhCloseQuery(g_fq); g_fq = 0; } }

// ------------------------------------------------------------------ sampling
static void sampleLite() {
    FILETIME i, k, u; GetSystemTimes(&i, &k, &u);
    ULONGLONG I = ft2u(i), tot = ft2u(k) + ft2u(u);
    if (g_prevTot && tot > g_prevTot) { double dt = (double)(tot - g_prevTot), di = (double)(I - g_prevIdle); g_val[M_CPU] = clampd(100.0 * (dt - di) / dt); }
    g_prevTot = tot; g_prevIdle = I;
    g_ms.dwLength = sizeof g_ms; GlobalMemoryStatusEx(&g_ms); g_val[M_MEM] = (double)g_ms.dwMemoryLoad;
    g_val[M_GPU] = readGpu();
}
static void sampleNet() {
    PMIB_IF_TABLE2 t = 0; if (GetIfTable2(&t) != NO_ERROR) return;
    ULONGLONG in = 0, out = 0; int up = 0;
    for (ULONG i = 0; i < t->NumEntries; i++) {
        auto& r = t->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK || r.OperStatus != IfOperStatusUp || !r.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        in += r.InOctets; out += r.OutOctets; up++;
    }
    FreeMibTable(t);
    ULONGLONG now = GetTickCount64(); double dt = g_netTick ? (now - g_netTick) / 1000.0 : 0;
    if (g_prevIn && dt > 0) { g_netRecv = in >= g_prevIn ? (in - g_prevIn) / dt : 0; g_netSend = out >= g_prevOut ? (out - g_prevOut) / dt : 0; }
    g_prevIn = in; g_prevOut = out; g_netTick = now; g_netUp = up; g_val[M_NET] = g_netRecv + g_netSend;
}
static void sampleFull() {
    sampleLite(); sampleNet();
    g_pi.cb = sizeof g_pi; GetPerformanceInfo(&g_pi, sizeof g_pi);
    if (g_fq) {
        PdhCollectQueryData(g_fq); PDH_FMT_COUNTERVALUE v;
        if (g_cDisk && PdhGetFormattedCounterValue(g_cDisk, PDH_FMT_DOUBLE, 0, &v) == 0) g_val[M_DISK] = clampd(v.doubleValue);
        if (g_cDiskB && PdhGetFormattedCounterValue(g_cDiskB, PDH_FMT_DOUBLE, 0, &v) == 0) g_diskBps = v.doubleValue;
        double tot = 0; if (g_cGpuMem) pdhArray(g_cGpuMem, nullptr, &tot); g_gpuMem = tot;
    }
}
static void pushHist() { for (int m = 0; m < M_N; m++) { memmove(g_hist[m], g_hist[m] + 1, (HN - 1) * sizeof(double)); g_hist[m][HN - 1] = g_val[m]; } }

// ------------------------------------------------------------------ list data
struct Row { std::wstring key; DWORD id = 0; std::wstring c[6]; double n[6] = {}; };
struct SInfo { HKEY root; std::wstring ap, name; bool dis; };
static std::vector<Row> g_rows; static std::vector<SInfo> g_sinfo; static std::unordered_map<DWORD, ULONGLONG> g_pcpu;
static int g_sortCol[5] = { 3, 0, 0, 0, 0 }; static bool g_sortAsc[5] = { false, true, true, true, true };
static bool g_hasSel, g_refreshing; static std::wstring g_selKey;

static const wchar_t* prioName(DWORD c) {
    switch (c) { case REALTIME_PRIORITY_CLASS: return L"Realtime"; case HIGH_PRIORITY_CLASS: return L"High"; case ABOVE_NORMAL_PRIORITY_CLASS: return L"Above normal";
    case NORMAL_PRIORITY_CLASS: return L"Normal"; case BELOW_NORMAL_PRIORITY_CLASS: return L"Below normal"; case IDLE_PRIORITY_CLASS: return L"Low"; } return L"-";
}
static void refreshProcs() {
    FILETIME nf; GetSystemTimeAsFileTime(&nf); ULONGLONG wall = ft2u(nf);
    double dw = g_lastWall ? (double)(wall - g_lastWall) : 0; g_lastWall = wall;
    std::unordered_map<DWORD, ULONGLONG> nowMap; g_rows.clear();
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (sn == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(sn, &pe); ok; ok = Process32NextW(sn, &pe)) {
        Row r; r.id = pe.th32ProcessID; r.key = std::to_wstring(r.id);
        double cpu = 0, mem = 0; DWORD pc = 0;
        if (r.id) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.id);
            if (h) {
                FILETIME c, e, k, u;
                if (GetProcessTimes(h, &c, &e, &k, &u)) {
                    ULONGLONG t = ft2u(k) + ft2u(u); nowMap[r.id] = t; auto f = g_pcpu.find(r.id);
                    if (f != g_pcpu.end() && dw > 0 && t >= f->second) cpu = clampd((t - f->second) / dw / g_ncpu * 100.0);
                }
                PROCESS_MEMORY_COUNTERS pmc; if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) mem = (double)pmc.WorkingSetSize;
                pc = GetPriorityClass(h); CloseHandle(h);
            }
        } else cpu = clampd(100.0 - g_val[M_CPU]);
        r.c[0] = pe.szExeFile; r.c[1] = std::to_wstring(r.id); r.c[2] = F(L"%.1f%%", cpu); r.c[3] = mem > 0 ? B(mem) : L"-";
        r.c[4] = std::to_wstring(pe.cntThreads); r.c[5] = prioName(pc);
        r.n[1] = r.id; r.n[2] = cpu; r.n[3] = mem; r.n[4] = pe.cntThreads;
        g_rows.push_back(std::move(r));
    }
    CloseHandle(sn); g_pcpu.swap(nowMap);
}
static void refreshServices() {
    g_rows.clear();
    SC_HANDLE sc = OpenSCManagerW(0, 0, SC_MANAGER_ENUMERATE_SERVICE); if (!sc) return;
    DWORD need = 0, cnt = 0, rs = 0;
    EnumServicesStatusExW(sc, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, 0, 0, &need, &cnt, &rs, 0);
    std::vector<BYTE> b(need + 4096); rs = 0;
    if (EnumServicesStatusExW(sc, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL, b.data(), (DWORD)b.size(), &need, &cnt, &rs, 0) || GetLastError() == ERROR_MORE_DATA) {
        auto* sv = (ENUM_SERVICE_STATUS_PROCESSW*)b.data();
        for (DWORD i = 0; i < cnt; i++) {
            Row r; r.id = i; r.key = sv[i].lpServiceName; r.c[0] = sv[i].lpServiceName;
            DWORD pid = sv[i].ServiceStatusProcess.dwProcessId; r.c[1] = pid ? std::to_wstring(pid) : L""; r.n[1] = pid; r.c[2] = sv[i].lpDisplayName;
            switch (sv[i].ServiceStatusProcess.dwCurrentState) { case SERVICE_RUNNING: r.c[3] = L"Running"; break; case SERVICE_STOPPED: r.c[3] = L"Stopped"; break;
            case SERVICE_PAUSED: r.c[3] = L"Paused"; break; default: r.c[3] = L"Pending"; }
            g_rows.push_back(std::move(r));
        }
    }
    CloseServiceHandle(sc);
}
static void refreshStartup() {
    g_rows.clear(); g_sinfo.clear();
    const wchar_t* RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    struct Src { HKEY r; const wchar_t* run; const wchar_t* ap; const wchar_t* loc; } src[] = {
        { HKEY_CURRENT_USER, RUN, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", L"HKCU\\Run" },
        { HKEY_LOCAL_MACHINE, RUN, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run", L"HKLM\\Run" },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32", L"HKLM\\Run (32-bit)" } };
    for (auto& sr : src) {
        HKEY k; if (RegOpenKeyExW(sr.r, sr.run, 0, KEY_READ, &k)) continue;
        for (DWORD i = 0;; i++) {
            wchar_t nm[256]; DWORD nl = 256, ty, dl; wchar_t d[1024]; dl = sizeof d - 2;
            if (RegEnumValueW(k, i, nm, &nl, 0, &ty, (BYTE*)d, &dl)) break;
            if (ty != REG_SZ && ty != REG_EXPAND_SZ) continue; d[dl / 2] = 0;
            BYTE ap[16]; DWORD al = sizeof ap;
            bool dis = RegGetValueW(sr.r, sr.ap, nm, RRF_RT_REG_BINARY, 0, ap, &al) == 0 && al > 0 && (ap[0] & 1);
            Row r; r.id = (DWORD)g_sinfo.size(); r.key = std::wstring(sr.loc) + L"|" + nm;
            r.c[0] = nm; r.c[1] = dis ? L"Disabled" : L"Enabled"; r.c[2] = d; r.c[3] = sr.loc;
            g_sinfo.push_back({ sr.r, sr.ap, nm, dis }); g_rows.push_back(std::move(r));
        }
        RegCloseKey(k);
    }
}
static bool isNumCol(int tab, int c) { return tab == 0 ? (c >= 1 && c <= 4) : (tab == 2 && c == 1); }
static void sortRows() {
    int c = g_sortCol[g_tab]; bool a = g_sortAsc[g_tab], nm = isNumCol(g_tab, c);
    std::stable_sort(g_rows.begin(), g_rows.end(), [&](const Row& x, const Row& y) {
        int r = nm ? (x.n[c] < y.n[c] ? -1 : (x.n[c] > y.n[c] ? 1 : 0)) : _wcsicmp(x.c[c].c_str(), y.c[c].c_str());
        return a ? r < 0 : r > 0; });
}
static void setSortArrow() {
    HWND hd = ListView_GetHeader(g_hList); int n = Header_GetItemCount(hd);
    for (int i = 0; i < n; i++) { HDITEMW hi = {}; hi.mask = HDI_FORMAT; Header_GetItem(hd, i, &hi); hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_sortCol[g_tab]) hi.fmt |= g_sortAsc[g_tab] ? HDF_SORTUP : HDF_SORTDOWN; Header_SetItem(hd, i, &hi); }
}
static void fillList() {
    g_refreshing = true; sortRows();
    ListView_SetItemCountEx(g_hList, (int)g_rows.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    int idx = -1; if (g_hasSel) for (size_t i = 0; i < g_rows.size(); i++) if (g_rows[i].key == g_selKey) { idx = (int)i; break; }
    if (g_hasSel && idx < 0) g_hasSel = false;
    if (ListView_GetNextItem(g_hList, -1, LVNI_SELECTED) != idx) {
        ListView_SetItemState(g_hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (idx >= 0) ListView_SetItemState(g_hList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    InvalidateRect(g_hList, 0, FALSE); setSortArrow(); g_refreshing = false;
}
static void refreshCurrent() { if (g_tab == 0) refreshProcs(); else if (g_tab == 2) refreshServices(); else if (g_tab == 3) refreshStartup(); else return; fillList(); }
static void setupColumns() {
    while (ListView_DeleteColumn(g_hList, 0)) {}
    struct C { const wchar_t* n; int w; bool r; };
    std::vector<C> c;
    if (g_tab == 0) c = { { L"Name", 260, 0 }, { L"PID", 70, 1 }, { L"CPU", 70, 1 }, { L"Memory", 100, 1 }, { L"Threads", 70, 1 }, { L"Priority", 110, 0 } };
    else if (g_tab == 2) c = { { L"Name", 190, 0 }, { L"PID", 70, 1 }, { L"Description", 360, 0 }, { L"Status", 100, 0 } };
    else c = { { L"Name", 200, 0 }, { L"Status", 90, 0 }, { L"Command", 480, 0 }, { L"Location", 140, 0 } };
    for (size_t i = 0; i < c.size(); i++) { LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; col.pszText = (LPWSTR)c[i].n; col.cx = s(c[i].w); col.fmt = c[i].r ? LVCFMT_RIGHT : LVCFMT_LEFT; ListView_InsertColumn(g_hList, (int)i, &col); }
}

// ------------------------------------------------------------------ actions
static Row* selRow() { if (!g_hasSel) return 0; for (auto& r : g_rows) if (r.key == g_selKey) return &r; return 0; }
static void msg(const std::wstring& t) { MessageBoxW(g_hMain, t.c_str(), L"Task Manager", MB_OK | MB_ICONINFORMATION); }
static void endTask() {
    Row* r = selRow(); if (!r) return;
    if (g_cfg[S_CONFIRM] && MessageBoxW(g_hMain, F(L"End \"%ls\" (PID %u)?\nUnsaved data in it will be lost.", r->c[0].c_str(), r->id).c_str(), L"End task", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, r->id);
    if (!h || !TerminateProcess(h, 1)) msg(L"Could not end this process (access denied). Try running Taskmgr as administrator.");
    if (h) CloseHandle(h); Sleep(80); refreshCurrent();
}
static void openLocation() {
    Row* r = selRow(); if (!r) return; std::wstring path;
    if (g_tab == 0) { HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r->id); if (!h) return; wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; if (QueryFullProcessImageNameW(h, 0, b, &n)) path = b; CloseHandle(h); }
    else if (g_tab == 3) { std::wstring c = r->c[2]; if (!c.empty() && c[0] == L'"') { size_t e = c.find(L'"', 1); path = c.substr(1, e - 1); } else path = c.substr(0, c.find(L".exe") == std::wstring::npos ? c.size() : c.find(L".exe") + 4); }
    if (!path.empty()) ShellExecuteW(0, L"open", L"explorer.exe", (L"/select,\"" + path + L"\"").c_str(), 0, SW_SHOW);
}
static void setPriority(DWORD cls) {
    Row* r = selRow(); if (!r || g_tab != 0) return; HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, r->id);
    if (!h || !SetPriorityClass(h, cls)) msg(L"Could not change the priority (access denied)."); if (h) CloseHandle(h); refreshCurrent();
}
static void toggleService() {
    Row* r = selRow(); if (!r) return; bool run = r->c[3] == L"Running";
    SC_HANDLE sc = OpenSCManagerW(0, 0, SC_MANAGER_CONNECT); SC_HANDLE sv = sc ? OpenServiceW(sc, r->key.c_str(), SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS) : 0;
    bool ok = false; if (sv) { SERVICE_STATUS st; ok = run ? ControlService(sv, SERVICE_CONTROL_STOP, &st) : StartServiceW(sv, 0, 0); }
    if (!ok) msg(L"The operation failed. Administrator rights are usually required to start or stop services.");
    if (sv) CloseServiceHandle(sv); if (sc) CloseServiceHandle(sc); Sleep(300); refreshCurrent();
}
static void toggleStartup() {
    Row* r = selRow(); if (!r) return; SInfo& si = g_sinfo[r->id]; HKEY k;
    if (RegCreateKeyExW(si.root, si.ap.c_str(), 0, 0, 0, KEY_SET_VALUE, 0, &k, 0)) { msg(L"Access denied. Run Taskmgr as administrator to change machine-wide entries."); return; }
    BYTE v[12] = {}; v[0] = si.dis ? 2 : 3; RegSetValueExW(k, si.name.c_str(), 0, REG_BINARY, v, 12); RegCloseKey(k); refreshCurrent();
}
static void doAction() { if (g_tab == 0) endTask(); else if (g_tab == 2) toggleService(); else if (g_tab == 3) toggleStartup(); }

// ------------------------------------------------------------------ tray
static int g_trayIconVal = -2, g_trayIconMode = -1; static std::wstring g_lastTip; static HICON g_ownIcon, g_baseIcon;
static HICON makeValueIcon(int val, COLORREF bg) {
    int sz = GetSystemMetrics(SM_CXSMICON); HDC dc = GetDC(0); HDC m = CreateCompatibleDC(dc);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits; HBITMAP cb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0); HGDIOBJ old = SelectObject(m, cb);
    RECT r = { 0, 0, sz, sz }; HBRUSH br = CreateSolidBrush(bg); FillRect(m, &r, br); DeleteObject(br);
    HFONT f = CreateFontW(-sz * 82 / 100, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI"); HGDIOBJ of = SelectObject(m, f);
    SetBkMode(m, TRANSPARENT); SetTextColor(m, RGB(255, 255, 255)); wchar_t t[8]; swprintf(t, 8, L"%d", val > 99 ? 99 : val);
    DrawTextW(m, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); GdiFlush();
    DWORD* p = (DWORD*)bits; for (int i = 0; i < sz * sz; i++) p[i] |= 0xFF000000;
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, NULL); ICONINFO ii = { TRUE, 0, 0, mask, cb }; HICON h = CreateIconIndirect(&ii);
    SelectObject(m, of); DeleteObject(f); SelectObject(m, old); DeleteObject(cb); DeleteObject(mask); DeleteDC(m); ReleaseDC(0, dc); return h;
}
static void trayRefresh(bool add, bool force = false) {
    int mode = g_cfg[S_TRAYICON]; int mi = mode == 1 ? M_CPU : mode == 2 ? M_MEM : M_GPU; int v = mode ? (int)(g_val[mi] + 0.5) : -1;
    std::wstring tip = g_cfg[S_TRAYHOVER] ? F(L"Task Manager\nCPU: %.0f%%\nRAM: %.0f%%\nGPU: %.0f%%", g_val[M_CPU], g_val[M_MEM], g_val[M_GPU]) : L"Task Manager";
    if (!add && !force && tip == g_lastTip && v == g_trayIconVal && mode == g_trayIconMode) return;
    NOTIFYICONDATAW n = {}; n.cbSize = sizeof n; n.hWnd = g_hTray; n.uID = 1; n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; n.uCallbackMessage = WM_TRAY;
    if (!g_baseIcon) { int z = GetSystemMetrics(SM_CXSMICON); g_baseIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, z, z, LR_SHARED); }
    HICON old = g_ownIcon; g_ownIcon = 0;
    if (mode) g_ownIcon = makeValueIcon(v, kCol[mi]);
    n.hIcon = g_ownIcon ? g_ownIcon : g_baseIcon; wcsncpy(n.szTip, tip.c_str(), 127);
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &n); if (old) DestroyIcon(old);
    g_lastTip = tip; g_trayIconVal = v; g_trayIconMode = mode;
}
static void applyTimer() {
    bool need = !g_quitting && (g_hMain || g_cfg[S_TRAYHOVER] || g_cfg[S_TRAYICON]);
    if (g_hTray) KillTimer(g_hTray, 1);
    if (need) { if (!g_timerOn) g_prevTot = 0; SetTimer(g_hTray, 1, g_hMain ? (g_cfg[S_REFRESH] + 1) * 1000 : 1000, 0); }
    else if (!g_hMain) gpuClose();
    g_timerOn = need;
}

// ------------------------------------------------------------------ DWM / theme
static void applyDwm(HWND h) {
    BOOL dark = g_dark; DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
    g_micaOn = false;
    if (g_cfg[S_MICA]) {
        int bt = 2; HRESULT hr = DwmSetWindowAttribute(h, 38, &bt, sizeof bt);          // DWMSBT_MAINWINDOW (Win11 22621+)
        if (FAILED(hr)) { BOOL on = TRUE; hr = DwmSetWindowAttribute(h, 1029, &on, sizeof on); }   // early Win11 Mica
        g_micaOn = SUCCEEDED(hr);
    }
    if (!g_micaOn) { int bt = 1; DwmSetWindowAttribute(h, 38, &bt, sizeof bt); }
    MARGINS m = { -1, -1, -1, -1 }; if (!g_micaOn) m = { 0, 0, 0, 0 };
    DwmExtendFrameIntoClientArea(h, &m); InvalidateRect(h, 0, TRUE);
}
struct Th { COLORREF bg, card, card2, text, sub, graph, grid; };
static Th th() { return g_dark ? Th{ RGB(32,32,32), RGB(46,46,46), RGB(58,58,58), RGB(255,255,255), RGB(172,172,172), RGB(30,30,30), RGB(60,60,60) }
                               : Th{ RGB(243,243,243), RGB(251,251,251), RGB(232,232,232), RGB(20,20,20), RGB(100,100,100), RGB(255,255,255), RGB(228,228,228) }; }
static void applyListTheme() {
    if (!g_hList) return; Th t = th();
    SetWindowTheme(g_hList, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowTheme(ListView_GetHeader(g_hList), g_dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
    ListView_SetBkColor(g_hList, t.card); ListView_SetTextBkColor(g_hList, t.card); ListView_SetTextColor(g_hList, t.text);
    InvalidateRect(g_hList, 0, TRUE);
}
static RECT contentRect() { RECT c; GetClientRect(g_hMain, &c); return { s(12), s(54), c.right - s(12), c.bottom - s(12) }; }
static void layout() {
    if (!g_hMain) return; RECT cr = contentRect(); bool lt = g_tab == 0 || g_tab == 2 || g_tab == 3;
    ShowWindow(g_hList, lt ? SW_SHOW : SW_HIDE);
    if (lt) SetWindowPos(g_hList, 0, cr.left, cr.top + s(46), cr.right - cr.left, cr.bottom - cr.top - s(46), SWP_NOZORDER);
}
static void switchTab(int t) {
    g_tab = t; g_hasSel = false; g_scroll = 0;
    if (t == 0 || t == 2 || t == 3) { ListView_SetItemCountEx(g_hList, 0, 0); setupColumns(); refreshCurrent(); }
    layout(); InvalidateRect(g_hMain, 0, FALSE);
}
static void applyCfg() {
    computeDark(); SetPriorityClass(GetCurrentProcess(), kPrioCls[g_cfg[S_PRIO]]); startupReg(g_cfg[S_STARTUP] != 0);
    if (g_hMain) {
        SetWindowPos(g_hMain, g_cfg[S_TOPMOST] ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        applyDwm(g_hMain); applyListTheme(); InvalidateRect(g_hMain, 0, TRUE);
    }
    applyTimer(); trayRefresh(false, true); saveCfg();
}

// ------------------------------------------------------------------ painting helpers
struct Hit { RectF r; int id, val; };
static std::vector<Hit> g_hits; static int g_hovId = -1, g_hovVal = -1;
static HDC g_bbDC; static HBITMAP g_bbBmp, g_bbOld; static int g_bbW, g_bbH;
static Color C(COLORREF c, int a = 255) { return Color((BYTE)a, GetRValue(c), GetGValue(c), GetBValue(c)); }
static void rrPath(GraphicsPath& p, RectF r, float d) {
    p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90); p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90); p.CloseFigure();
}
static void fillRR(Graphics& g, COLORREF c, RectF r, float rad, int a = 255) { GraphicsPath p; rrPath(p, r, rad * 2); SolidBrush b(C(c, a)); g.FillPath(&b, &p); }
static void text(Graphics& g, const std::wstring& t, float px, bool bold, COLORREF col, float x, float y, float w = 0, int al = 0) {
    FontFamily ff(L"Segoe UI"); Font f(&ff, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel); SolidBrush b(C(col));
    StringFormat sf(StringFormatFlagsNoWrap); sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetAlignment(al == 0 ? StringAlignmentNear : al == 1 ? StringAlignmentCenter : StringAlignmentFar);
    RectF r(x, y, w > 0 ? w : 3000.f, px * 1.7f); g.DrawString(t.c_str(), -1, &f, r, &sf, &b);
}
static void addHit(RectF r, int id, int val) { g_hits.push_back({ r, id, val }); }
static bool hov(int id, int val) { return g_hovId == id && g_hovVal == val; }

static void drawGraph(Graphics& g, RectF r, int m, bool big) {
    Th t = th(); SolidBrush bg(C(t.graph)); g.FillRectangle(&bg, r);
    if (big) { Pen gp(C(t.grid), 1); for (int i = 1; i < 10; i++) { float x = r.X + r.Width * i / 10, y = r.Y + r.Height * i / 10; g.DrawLine(&gp, x, r.Y, x, r.GetBottom()); g.DrawLine(&gp, r.X, y, r.GetRight(), y); } }
    double mx = 100; if (m == M_NET) { mx = 100 * 1024; for (int i = 0; i < HN; i++) if (g_hist[m][i] > mx) mx = g_hist[m][i]; }
    std::vector<PointF> pts; for (int i = 0; i < HN; i++) pts.push_back(PointF(r.X + r.Width * i / (HN - 1), (REAL)(r.GetBottom() - (r.Height - 1) * clampd(g_hist[m][i] / mx, 0, 1))));
    std::vector<PointF> poly = pts; poly.push_back(PointF(r.GetRight(), r.GetBottom())); poly.push_back(PointF(r.X, r.GetBottom()));
    g.SetClip(r); SolidBrush fb(C(kCol[m], 70)); g.FillPolygon(&fb, poly.data(), (INT)poly.size());
    Pen lp(C(kCol[m]), big ? 2.f : 1.2f); g.DrawLines(&lp, pts.data(), (INT)pts.size()); g.ResetClip();
    Pen bp(C(kCol[m]), 1.f); g.DrawRectangle(&bp, r.X, r.Y, r.Width - 1, r.Height - 1);
}
static std::wstring uptime() { ULONGLONG ms = GetTickCount64() / 1000; return F(L"%llu:%02llu:%02llu:%02llu", ms / 86400, ms / 3600 % 24, ms / 60 % 60, ms % 60); }
static std::wstring perfValue(int m) {
    switch (m) {
    case M_CPU: return F(L"%.0f%%  %.2f GHz", g_val[M_CPU], g_cpuMHz / 1000.0);
    case M_MEM: return F(L"%.1f/%.1f GB (%.0f%%)", (g_ms.ullTotalPhys - g_ms.ullAvailPhys) / 1073741824.0, g_ms.ullTotalPhys / 1073741824.0, g_val[M_MEM]);
    case M_DISK: return F(L"%.0f%%", g_val[M_DISK]);
    case M_NET: return F(L"R: %ls  S: %ls", B(g_netRecv).c_str(), B(g_netSend).c_str());
    default: return g_gpuOk ? F(L"%.0f%%", g_val[M_GPU]) : L"N/A";
    }
}
typedef std::vector<std::pair<std::wstring, std::wstring>> Stats;
static Stats perfStats(int m, std::wstring& sub) {
    Stats v;
    if (m == M_CPU) { sub = g_cpuName; v = { { L"Utilization", F(L"%.0f%%", g_val[M_CPU]) }, { L"Speed", F(L"%.2f GHz", g_cpuMHz / 1000.0) }, { L"Logical processors", std::to_wstring(g_ncpu) },
        { L"Processes", std::to_wstring(g_pi.ProcessCount) }, { L"Threads", std::to_wstring(g_pi.ThreadCount) }, { L"Handles", std::to_wstring(g_pi.HandleCount) }, { L"Up time", uptime() } }; }
    else if (m == M_MEM) { double ps = (double)g_pi.PageSize; sub = B((double)g_ms.ullTotalPhys) + L" total";
        v = { { L"In use", B((double)(g_ms.ullTotalPhys - g_ms.ullAvailPhys)) }, { L"Available", B((double)g_ms.ullAvailPhys) }, { L"Usage", F(L"%.0f%%", g_val[M_MEM]) },
        { L"Committed", B(g_pi.CommitTotal * ps) + L" / " + B(g_pi.CommitLimit * ps) }, { L"Cached", B(g_pi.SystemCache * ps) }, { L"Total memory", B((double)g_ms.ullTotalPhys) } }; }
    else if (m == M_DISK) { sub = L"All physical disks"; ULARGE_INTEGER fr, tot, x; wchar_t sd[MAX_PATH]; GetSystemDirectoryW(sd, MAX_PATH); sd[3] = 0; GetDiskFreeSpaceExW(sd, &fr, &tot, &x);
        v = { { L"Active time", F(L"%.0f%%", g_val[M_DISK]) }, { L"Throughput", B(g_diskBps) + L"/s" }, { L"System drive free", B((double)fr.QuadPart) + L" of " + B((double)tot.QuadPart) } }; }
    else if (m == M_NET) { sub = L"All network adapters"; v = { { L"Receive", B(g_netRecv) + L"/s" }, { L"Send", B(g_netSend) + L"/s" }, { L"Total", B(g_netRecv + g_netSend) + L"/s" }, { L"Active adapters", std::to_wstring(g_netUp) } }; }
    else { sub = g_gpuName; v = { { L"Utilization", g_gpuOk ? F(L"%.0f%%", g_val[M_GPU]) : L"Not available" }, { L"Dedicated GPU memory in use", B(g_gpuMem) } }; }
    return v;
}

static void paintSettings(Graphics& g, RECT cr, const Th& t) {
    struct SI { int id; const wchar_t* title; const wchar_t* desc; std::vector<const wchar_t*> opts; };
    static const std::vector<SI> items = {
        { -1, L"Appearance", L"", {} },
        { S_MICA, L"Mica effects", L"Translucent Mica material on the title bar and app background (Windows 11)", {} },
        { S_THEME, L"Theme", L"Follow Windows, or force light / dark", { L"System", L"Light", L"Dark" } },
        { S_TOPMOST, L"Always on top", L"Keep Task Manager above other windows", {} },
        { -1, L"Behavior", L"", {} },
        { S_CLOSE, L"Close to tray", L"Closing the window sends it to the tray and frees its memory", {} },
        { S_STARTUP, L"Tray opens with Windows", L"Start minimized to the tray when you sign in", {} },
        { S_CONFIRM, L"Confirm before ending a task", L"Ask before terminating a process", {} },
        { -1, L"Performance", L"", {} },
        { S_REFRESH, L"Refresh speed", L"Update interval while the window is open", { L"1 sec", L"2 sec", L"3 sec" } },
        { S_PRIO, L"Process priority", L"CPU priority of Taskmgr itself", { L"Low", L"Below normal", L"Normal" } },
        { -1, L"Tray", L"", {} },
        { S_TRAYHOVER, L"Hover summary", L"Show CPU, RAM and GPU usage when hovering the tray icon", {} },
        { S_TRAYICON, L"Usage in tray icon", L"Draw a live percentage on the tray icon", { L"Off", L"CPU", L"RAM", L"GPU" } } };
    float W = (float)(std::min)((int)(cr.right - cr.left), s(880)), x = (float)cr.left, y = (float)cr.top - g_scroll;
    g.SetClip(RectF((REAL)cr.left, (REAL)cr.top, (REAL)(cr.right - cr.left), (REAL)(cr.bottom - cr.top)));
    text(g, L"Info", (float)s(15), true, t.text, x, y); y += s(30);
    fillRR(g, t.card, RectF(x, y, W, (float)s(92)), (float)s(6));
    fillRR(g, ACCENT, RectF(x + s(16), y + s(16), (float)s(60), (float)s(60)), (float)s(12));
    text(g, L"T", (float)s(34), true, RGB(255,255,255), x + s(16), y + s(24), (float)s(60), 1);
    text(g, L"Taskmgr", (float)s(22), true, t.text, x + s(92), y + s(12));
    text(g, L"Built by Subhajit Maji :)", (float)s(14), false, t.text, x + s(92), y + s(44));
    text(g, L"Version 1.0.0.0  \u00B7  Task Manager", (float)s(12), false, t.sub, x + s(92), y + s(66));
    y += s(104);
    for (auto& it : items) {
        if (it.id < 0) { y += s(8); text(g, it.title, (float)s(15), true, t.text, x, y); y += s(30); continue; }
        RectF r(x, y, W, (float)s(56)); fillRR(g, t.card, r, (float)s(6));
        float ow = (float)s(84), cw = it.opts.empty() ? (float)s(44) : ow * it.opts.size();
        text(g, it.title, (float)s(14), false, t.text, x + s(16), y + s(8), W - cw - s(48));
        if (*it.desc) text(g, it.desc, (float)s(12), false, t.sub, x + s(16), y + s(30), W - cw - s(48));
        int cur = g_cfg[it.id];
        if (it.opts.empty()) {
            RectF tr(x + W - s(16) - s(44), y + s(17), (float)s(44), (float)s(22)); SolidBrush wb(C(RGB(255,255,255)));
            if (cur) { fillRR(g, ACCENT, tr, (float)s(11)); g.FillEllipse(&wb, tr.GetRight() - s(19), tr.Y + s(4), (REAL)s(14), (REAL)s(14)); }
            else { GraphicsPath p; rrPath(p, tr, (float)s(22)); Pen pn(C(t.sub), 1.2f); g.DrawPath(&pn, &p); SolidBrush sb(C(t.sub)); g.FillEllipse(&sb, tr.X + s(4), tr.Y + s(4), (REAL)s(14), (REAL)s(14)); }
            addHit(tr, H_SET + it.id, -1);
        } else {
            float cx = x + W - s(16) - cw; fillRR(g, t.card2, RectF(cx, y + s(12), cw, (float)s(32)), (float)s(6));
            for (size_t i = 0; i < it.opts.size(); i++) {
                RectF o(cx + ow * i, y + s(12), ow, (float)s(32)); bool sel = (int)i == cur;
                if (sel) fillRR(g, ACCENT, RectF(o.X + 2, o.Y + 2, o.Width - 4, o.Height - 4), (float)s(5));
                text(g, it.opts[i], (float)s(12), false, sel ? RGB(255,255,255) : t.text, o.X, o.Y + s(7), o.Width, 1); addHit(o, H_SET + it.id, (int)i);
            }
        }
        y += s(62);
    }
    g_scrollMax = (std::max)(0.f, y + g_scroll - cr.bottom + s(8)); g.ResetClip();
}

static void paintMain(HWND h, HDC dc) {
    RECT rc; GetClientRect(h, &rc); int W = rc.right, H = rc.bottom; if (W < 1 || H < 1) return;
    if (!g_bbDC || g_bbW != W || g_bbH != H) {
        if (g_bbDC) { SelectObject(g_bbDC, g_bbOld); DeleteObject(g_bbBmp); DeleteDC(g_bbDC); }
        g_bbDC = CreateCompatibleDC(dc); BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* bits; g_bbBmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0); g_bbOld = (HBITMAP)SelectObject(g_bbDC, g_bbBmp); g_bbW = W; g_bbH = H;
    }
    {
        Graphics g(g_bbDC); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHalf);
        Th t = th(); g_hits.clear();
        g.SetCompositingMode(CompositingModeSourceCopy);
        SolidBrush bgb(g_micaOn ? Color(0, 0, 0, 0) : C(t.bg)); g.FillRectangle(&bgb, 0, 0, W, H);   // alpha 0 => Mica shows through
        g.SetCompositingMode(CompositingModeSourceOver);
        RECT cr = contentRect(); float cl = (float)cr.left, ct = (float)cr.top, cw = (float)(cr.right - cr.left), ch = (float)(cr.bottom - cr.top);
        // ---- tabs
        static const wchar_t* tabs[5] = { L"Processes", L"Performance", L"Services", L"Startup", L"Settings" };
        for (int i = 0; i < 5; i++) {
            RectF r((float)s(12 + i * 118), (float)s(8), (float)s(112), (float)s(38));
            if (i == g_tab) fillRR(g, t.card, r, (float)s(6)); else if (hov(H_TAB, i)) fillRR(g, t.card2, r, (float)s(6), 150);
            text(g, tabs[i], (float)s(14), i == g_tab, t.text, r.X, r.Y + s(9), r.Width, 1);
            if (i == g_tab) fillRR(g, ACCENT, RectF(r.X + r.Width / 2 - s(10), r.GetBottom() - s(5), (float)s(20), (float)s(3)), 1.5f);
            addHit(r, H_TAB, i);
        }
        // ---- list tabs: title strip + action button
        if (g_tab == 0 || g_tab == 2 || g_tab == 3) {
            std::wstring title = g_tab == 0 ? F(L"Processes (%d)", (int)g_rows.size()) : g_tab == 2 ? F(L"Services (%d)", (int)g_rows.size()) : F(L"Startup apps (%d)", (int)g_rows.size());
            text(g, title, (float)s(20), true, t.text, cl, ct + s(2));
            Row* sr = selRow(); std::wstring lbl = g_tab == 0 ? L"End task" : g_tab == 2 ? (sr && sr->c[3] == L"Running" ? L"Stop service" : L"Start service") : (sr && sr->c[1] == L"Disabled" ? L"Enable" : L"Disable");
            RectF b(cl + cw - s(130), ct, (float)s(130), (float)s(34)); bool en = sr != nullptr;
            fillRR(g, en ? (hov(H_ACT, 0) ? RGB(0, 99, 177) : ACCENT) : t.card2, b, (float)s(6));
            text(g, lbl, (float)s(13), false, en ? RGB(255,255,255) : t.sub, b.X, b.Y + s(8), b.Width, 1); if (en) addHit(b, H_ACT, 0);
            if (g_tab == 0) text(g, F(L"CPU %.0f%%    Memory %.0f%%    Disk %.0f%%    GPU %.0f%%", g_val[M_CPU], g_val[M_MEM], g_val[M_DISK], g_val[M_GPU]), (float)s(13), false, t.sub, cl + cw - s(150) - s(520), ct + s(8), (float)s(520), 2);
        }
        // ---- performance
        if (g_tab == 1) {
            float sw = (float)s(224);
            for (int m = 0; m < M_N; m++) {
                RectF r(cl, ct + m * s(68), sw, (float)s(64)); bool sel = m == g_perfSel;
                if (sel) fillRR(g, t.card, r, (float)s(6)); else if (hov(H_PERF, m)) fillRR(g, t.card2, r, (float)s(6), 150);
                if (sel) fillRR(g, kCol[m], RectF(r.X, r.Y + s(14), 3.f, (float)s(36)), 1.5f);
                drawGraph(g, RectF(r.X + s(12), r.Y + s(10), (float)s(60), (float)s(44)), m, false);
                text(g, kMName[m], (float)s(14), true, t.text, r.X + s(82), r.Y + s(9), sw - s(88)); text(g, perfValue(m), (float)s(11), false, t.sub, r.X + s(82), r.Y + s(32), sw - s(88));
                addHit(r, H_PERF, m);
            }
            float px = cl + sw + s(16), pw = cw - sw - s(16); int m = g_perfSel; std::wstring sub; Stats st = perfStats(m, sub);
            text(g, kMName[m], (float)s(24), true, t.text, px, ct - s(2)); text(g, sub, (float)s(13), false, t.sub, px + s(140), ct + s(8), pw - s(140), 2);
            float gh = (float)(std::max)(s(150), (std::min)(s(340), (int)(ch * 0.45f))), gy = ct + s(44);
            text(g, m == M_NET ? L"Throughput" : L"% Utilization", (float)s(11), false, t.sub, px, gy); text(g, F(L"%d seconds", HN * (g_cfg[S_REFRESH] + 1)), (float)s(11), false, t.sub, px, gy, pw, 2);
            drawGraph(g, RectF(px, gy + s(18), pw, gh), m, true);
            float sy = gy + s(18) + gh + s(22), cwid = pw / 3;
            for (size_t i = 0; i < st.size(); i++) {
                float ccx = px + (i % 3) * cwid, ccy = sy + (i / 3) * s(62);
                text(g, st[i].first, (float)s(12), false, t.sub, ccx, ccy, cwid - s(8)); text(g, st[i].second, (float)s(19), false, t.text, ccx, ccy + s(18), cwid - s(8));
            }
        }
        if (g_tab == 4) paintSettings(g, cr, t);
    }
    BitBlt(dc, 0, 0, W, H, g_bbDC, 0, 0, SRCCOPY);
}

// ------------------------------------------------------------------ main window
static void readHwInfo() {
    wchar_t b[128] = {}; DWORD sz = sizeof b; RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", RRF_RT_REG_SZ, 0, b, &sz);
    g_cpuName = b; g_cpuName.erase(0, g_cpuName.find_first_not_of(L' ')); sz = 4; g_cpuMHz = 0; RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", RRF_RT_REG_DWORD, 0, &g_cpuMHz, &sz);
    DISPLAY_DEVICEW dd = {}; dd.cb = sizeof dd; g_gpuName = EnumDisplayDevicesW(0, 0, &dd, 0) ? dd.DeviceString : L"GPU";
    SYSTEM_INFO si; GetSystemInfo(&si); g_ncpu = si.dwNumberOfProcessors;
}
static void makeFont() { if (g_font) DeleteObject(g_font); g_font = CreateFontW(-s(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); }
static void contextMenu(POINT pt) {
    Row* r = selRow(); if (!r) return; HMENU m = CreatePopupMenu();
    if (g_tab == 0) { AppendMenuW(m, MF_STRING, IDM_ACT, L"End task"); AppendMenuW(m, MF_STRING, IDM_LOC, L"Open file location");
        HMENU p = CreatePopupMenu(); const wchar_t* nm[] = { L"High", L"Above normal", L"Normal", L"Below normal", L"Low" };
        for (int i = 0; i < 5; i++) AppendMenuW(p, MF_STRING, IDM_PRIO + i, nm[i]); AppendMenuW(m, MF_POPUP, (UINT_PTR)p, L"Set priority"); }
    else if (g_tab == 2) AppendMenuW(m, MF_STRING, IDM_ACT, r->c[3] == L"Running" ? L"Stop" : L"Start");
    else { AppendMenuW(m, MF_STRING, IDM_ACT, r->c[1] == L"Disabled" ? L"Enable" : L"Disable"); AppendMenuW(m, MF_STRING, IDM_LOC, L"Open file location"); }
    int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hMain, 0); DestroyMenu(m);
    static const DWORD pc[5] = { HIGH_PRIORITY_CLASS, ABOVE_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS };
    if (c == IDM_ACT) doAction(); else if (c == IDM_LOC) openLocation(); else if (c >= IDM_PRIO && c < IDM_PRIO + 5) setPriority(pc[c - IDM_PRIO]);
}
static void onHit(int id, int val) {
    if (id == H_TAB) switchTab(val);
    else if (id == H_PERF) { g_perfSel = val; InvalidateRect(g_hMain, 0, FALSE); }
    else if (id == H_ACT) doAction();
    else if (id >= H_SET) { int sid = id - H_SET; if (val < 0) g_cfg[sid] ^= 1; else g_cfg[sid] = val; applyCfg(); }
}
static void cleanupMain() {   // give everything back while sitting in the tray
    std::vector<Row>().swap(g_rows); std::vector<SInfo>().swap(g_sinfo); std::unordered_map<DWORD, ULONGLONG>().swap(g_pcpu); std::vector<Hit>().swap(g_hits);
    if (g_bbDC) { SelectObject(g_bbDC, g_bbOld); DeleteObject(g_bbBmp); DeleteDC(g_bbDC); g_bbDC = 0; g_bbBmp = 0; g_bbW = g_bbH = 0; }
    if (g_font) { DeleteObject(g_font); g_font = 0; } fullClose();
    if (g_gdipOn) { GdiplusShutdown(g_gdipTok); g_gdipOn = false; }
    g_lastWall = 0; g_prevIn = 0; g_netTick = 0; g_hasSel = false;
}
static void trimMemory() { HeapCompact(GetProcessHeap(), 0); SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1); EmptyWorkingSet(GetCurrentProcess()); }
static void quitApp();

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hMain = h; g_dpi = GetDpiForWindow(h); makeFont();
        g_hList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, h, (HMENU)1, g_hInst, 0);
        ListView_SetExtendedListViewStyle(g_hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);
        fullOpen(); readHwInfo(); sampleFull(); applyCfg(); switchTab(g_tab); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paintMain(h, dc); EndPaint(h, &ps); return 0; }
    case WM_SIZE: layout(); InvalidateRect(h, 0, FALSE); return 0;
    case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = s(860); mm->ptMinTrackSize.y = s(580); return 0; }
    case WM_DPICHANGED: { g_dpi = HIWORD(w); auto* r = (RECT*)l; makeFont(); SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_font, TRUE);
        SetWindowPos(h, 0, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); if (g_tab == 0 || g_tab == 2 || g_tab == 3) { setupColumns(); fillList(); } return 0; }
    case WM_SETTINGCHANGE: if (g_cfg[S_THEME] == 0) applyCfg(); return 0;
    case WM_MOUSEMOVE: {
        static bool tr = false; if (!tr) { TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te); tr = true; }
        PointF p((REAL)GET_X_LPARAM(l), (REAL)GET_Y_LPARAM(l)); int hi = -1, hv = -1; RECT cr = contentRect();
        for (auto& x : g_hits) if (x.r.Contains(p) && (x.id < H_SET || PtInRect(&cr, { (LONG)p.X, (LONG)p.Y }))) { hi = x.id; hv = x.val; break; }
        if (hi != g_hovId || hv != g_hovVal) { g_hovId = hi; g_hovVal = hv; InvalidateRect(h, 0, FALSE); } tr = false; return 0; }
    case WM_MOUSELEAVE: g_hovId = g_hovVal = -1; InvalidateRect(h, 0, FALSE); return 0;
    case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && g_hovId >= 0) { SetCursor(LoadCursor(0, IDC_HAND)); return TRUE; } break;
    case WM_LBUTTONDOWN: { PointF p((REAL)GET_X_LPARAM(l), (REAL)GET_Y_LPARAM(l)); RECT cr = contentRect();
        for (auto x : g_hits) if (x.r.Contains(p) && (x.id < H_SET || PtInRect(&cr, { (LONG)p.X, (LONG)p.Y }))) { onHit(x.id, x.val); break; } return 0; }
    case WM_MOUSEWHEEL: if (g_tab == 4) { g_scroll = (float)clampd(g_scroll - GET_WHEEL_DELTA_WPARAM(w) / 120.0 * s(60), 0, g_scrollMax); InvalidateRect(h, 0, FALSE); } return 0;
    case WM_CONTEXTMENU: if ((HWND)w == g_hList) { POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; if (pt.x == -1) { RECT r; GetWindowRect(g_hList, &r); pt = { r.left + 40, r.top + 40 }; } contextMenu(pt); return 0; } break;
    case WM_NOTIFY: {
        auto* nh = (NMHDR*)l; if (nh->hwndFrom != g_hList) break;
        if (nh->code == LVN_GETDISPINFOW) { auto* di = (NMLVDISPINFOW*)l; int i = di->item.iItem, sub = di->item.iSubItem;
            if ((di->item.mask & LVIF_TEXT) && i >= 0 && i < (int)g_rows.size() && sub >= 0 && sub < 6) di->item.pszText = (LPWSTR)g_rows[i].c[sub].c_str(); }
        else if (nh->code == LVN_COLUMNCLICK) { int c = ((NMLISTVIEW*)l)->iSubItem; if (c == g_sortCol[g_tab]) g_sortAsc[g_tab] = !g_sortAsc[g_tab]; else { g_sortCol[g_tab] = c; g_sortAsc[g_tab] = !isNumCol(g_tab, c); } fillList(); }
        else if (nh->code == LVN_ITEMCHANGED && !g_refreshing) { auto* nm = (NMLISTVIEW*)l; if (nm->uChanged & LVIF_STATE) {
            if ((nm->uNewState & LVIS_SELECTED) && nm->iItem >= 0 && nm->iItem < (int)g_rows.size()) { g_hasSel = true; g_selKey = g_rows[nm->iItem].key; InvalidateRect(h, 0, FALSE); }
            else if ((nm->uOldState & LVIS_SELECTED) && !(nm->uNewState & LVIS_SELECTED)) { g_hasSel = false; InvalidateRect(h, 0, FALSE); } } }
        return 0; }
    case WM_CLOSE: if (g_cfg[S_CLOSE]) DestroyWindow(h); else quitApp(); return 0;
    case WM_DESTROY: g_hMain = 0; g_hList = 0; cleanupMain(); applyTimer(); trayRefresh(false, true); trimMemory(); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void showMain() {
    if (g_hMain) { if (IsIconic(g_hMain)) ShowWindow(g_hMain, SW_RESTORE); SetForegroundWindow(g_hMain); return; }
    if (!g_gdipOn) { GdiplusStartupInput in; GdiplusStartup(&g_gdipTok, &in, nullptr); g_gdipOn = true; }
    UINT d = GetDpiForSystem(); int w = MulDiv(1060, d, 96), hh = MulDiv(720, d, 96);
    CreateWindowExW(0, L"TaskmgrMain", L"Task Manager", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, w, hh, 0, 0, g_hInst, 0);
    if (g_hMain) SetForegroundWindow(g_hMain);
}
static void quitApp() {
    g_quitting = true; if (g_hMain) DestroyWindow(g_hMain);
    NOTIFYICONDATAW n = {}; n.cbSize = sizeof n; n.hWnd = g_hTray; n.uID = 1; Shell_NotifyIconW(NIM_DELETE, &n);
    if (g_ownIcon) DestroyIcon(g_ownIcon); DestroyWindow(g_hTray);
}

// ------------------------------------------------------------------ tray window
static LRESULT CALLBACK TrayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_wmTaskbar && m) { trayRefresh(true, true); return 0; }
    switch (m) {
    case WM_TIMER: if (g_hMain) sampleFull(); else sampleLite(); pushHist();
        if (g_hMain) { refreshCurrent(); InvalidateRect(g_hMain, 0, FALSE); } trayRefresh(false); return 0;
    case WM_TRAY: if (LOWORD(l) == WM_LBUTTONUP) showMain();
        else if (LOWORD(l) == WM_RBUTTONUP) { POINT p; GetCursorPos(&p); HMENU mn = CreatePopupMenu(); AppendMenuW(mn, MF_STRING, IDM_OPEN, L"Open Task Manager"); AppendMenuW(mn, MF_SEPARATOR, 0, 0); AppendMenuW(mn, MF_STRING, IDM_EXIT, L"Exit");
            SetForegroundWindow(h); int c = TrackPopupMenu(mn, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, 0); DestroyMenu(mn); PostMessageW(h, WM_NULL, 0, 0);
            if (c == IDM_OPEN) showMain(); else if (c == IDM_EXIT) quitApp(); }
        return 0;
    case WM_SHOWMAIN: showMain(); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmd, int) {
    g_hInst = hi;
    HANDLE mtx = CreateMutexW(0, TRUE, L"Local\\Taskmgr_SubhajitMaji");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { HWND o = FindWindowW(L"TaskmgrTrayWnd", 0); if (o) PostMessageW(o, WM_SHOWMAIN, 0, 0); return 0; }
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&ic);
    if (HMODULE u = GetModuleHandleW(L"uxtheme.dll")) {   // dark popup menus (undocumented ordinals 135/136)
        typedef int(WINAPI * SetMode)(int); typedef void(WINAPI * Flush)(); if (auto f = (SetMode)GetProcAddress(u, MAKEINTRESOURCEA(135))) f(1); if (auto f = (Flush)GetProcAddress(u, MAKEINTRESOURCEA(136))) f(); }
    loadCfg(); computeDark(); SetPriorityClass(GetCurrentProcess(), kPrioCls[g_cfg[S_PRIO]]);
    HICON icon = LoadIconW(hi, MAKEINTRESOURCEW(IDI_APP));
    WNDCLASSEXW wc = { sizeof wc }; wc.hInstance = hi; wc.hCursor = LoadCursor(0, IDC_ARROW); wc.hIcon = icon; wc.hIconSm = icon;
    wc.lpfnWndProc = MainProc; wc.lpszClassName = L"TaskmgrMain"; RegisterClassExW(&wc);
    wc.lpfnWndProc = TrayProc; wc.lpszClassName = L"TaskmgrTrayWnd"; RegisterClassExW(&wc);
    g_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    g_hTray = CreateWindowExW(WS_EX_TOOLWINDOW, L"TaskmgrTrayWnd", L"Taskmgr", WS_POPUP, 0, 0, 0, 0, 0, 0, hi, 0);
    SYSTEM_INFO si; GetSystemInfo(&si); g_ncpu = si.dwNumberOfProcessors;
    trayRefresh(true, true); applyTimer();
    if (!wcsstr(cmd, L"/tray")) showMain(); else trimMemory();
    MSG msg; while (GetMessageW(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CloseHandle(mtx); return 0;
}

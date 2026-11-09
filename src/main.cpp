// Magic Splash Wand - C++ / Win32 GUI
//
// C++ port of "Magic Splash Wand" (PHP) by NekoYuzu (MlgmXyysd), MeowCat Studio and
// Meow Mobile - https://github.com/MlgmXyysd/Magic-Splash-Wand
// Unpack / repack splash images of Qualcomm (and OPlus Qualcomm) devices.
//
// Pure Win32 API + GDI+ (image decoding) + zlib (gzip). No other runtime needed.

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "splash.h"

namespace {

// ---------------------------------------------------------------------------
// IDs / messages
// ---------------------------------------------------------------------------
enum {
    ID_RB_QC = 100, ID_RB_QP, ID_RB_OP,
    ID_IN_UNPACK, ID_BR_UNPACK_IN, ID_OUT_UNPACK, ID_BR_UNPACK_OUT, ID_BTN_UNPACK, ID_BTN_OPEN_OUT,
    ID_IN_REPACK, ID_BR_REPACK_IN, ID_OUT_REPACK, ID_BR_REPACK_OUT, ID_CHK_RLE, ID_BTN_REPACK,
    ID_BTN_SHOW_IMG,
    ID_LIST, ID_LOG, ID_PROGRESS, ID_BTN_CLEAR, ID_BTN_ABOUT
};

constexpr UINT WM_APP_LOG = WM_APP + 1;       // wParam = LogLevel, lParam = std::string*
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;  // wParam = done, lParam = total
constexpr UINT WM_APP_DONE = WM_APP + 3;      // lParam = DoneMsg*

const wchar_t* kMainClass = L"MagicSplashWandMain";
const wchar_t* kPreviewClass = L"MagicSplashWandPreview";
const wchar_t* kAppTitle = L"Magic Splash Wand";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
struct App {
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    int dpi = 96;
    HFONT font = nullptr, mono = nullptr;
    bool busy = false;

    HWND grpFmt, rbQC, rbQP, rbOP;
    HWND grpUnpack, lblUIn, eUIn, bUIn, lblUOut, eUOut, bUOut, bUnpack, bOpenOut;
    HWND grpRepack, lblRIn, eRIn, bRIn, lblROut, eROut, bROut, chkRle, bRepack, bShowImg;
    HWND grpImages, list, preview;
    HWND log, progress, bClear, bAbout;

    std::vector<fs::path> listPaths;
    fs::path lastUnpackDir, lastRepackFile;

    // preview
    bool pvHas = false;
    int pvW = 0, pvH = 0;
    Bytes pvDib;  // 24-bit BGR, top-down, rows padded to 4 bytes
    std::wstring pvText;
} g;

struct Job {
    HWND hwnd;
    bool unpack;
    Format fmt;
    fs::path in, out;
    RepackOptions opt;
};

struct DoneMsg {
    bool ok;
    bool unpack;
    fs::path out;
    std::vector<fs::path> files;
};

int S(int v) { return MulDiv(v, g.dpi, 96); }

std::wstring W(const std::string& u8) {
    if (u8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), &w[0], n);
    return w;
}

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

std::wstring trimPath(std::wstring s) {
    auto bad = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'"' || c == L'\r' || c == L'\n'; };
    while (!s.empty() && bad(s.back())) s.pop_back();
    size_t a = 0;
    while (a < s.size() && bad(s[a])) a++;
    return s.substr(a);
}

void msg(const wchar_t* text, UINT icon = MB_ICONINFORMATION) { MessageBoxW(g.hwnd, text, kAppTitle, MB_OK | icon); }

// ---------------------------------------------------------------------------
// Log / progress
// ---------------------------------------------------------------------------
void appendLog(LogLevel lv, const std::string& u8) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t ts[32];
    swprintf(ts, 32, L"[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    const wchar_t* tag = L"";
    switch (lv) {
        case LogLevel::Warn: tag = L"[WARN] "; break;
        case LogLevel::Error: tag = L"[ERROR] "; break;
        case LogLevel::Success: tag = L"[OK] "; break;
        default: break;
    }
    std::wstring line = std::wstring(ts) + tag + W(u8) + L"\r\n";
    int len = GetWindowTextLengthW(g.log);
    SendMessageW(g.log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

void logLocal(LogLevel lv, const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, nullptr, nullptr);
    appendLog(lv, u);
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------
bool browseOpenFile(std::wstring& path) {
    wchar_t buf[2048] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Splash images (*.img;*.bin)\0*.img;*.bin\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.lpstrTitle = L"Select splash image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

bool browseSaveFile(std::wstring& path) {
    wchar_t buf[2048] = L"splash.img";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"Splash images (*.img)\0*.img\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.lpstrDefExt = L"img";
    ofn.lpstrTitle = L"Save splash image as";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

bool browseFolder(const wchar_t* title, std::wstring& path) {
    wchar_t buf[MAX_PATH * 2] = L"";
    BROWSEINFOW bi{};
    bi.hwndOwner = g.hwnd;
    bi.lpszTitle = title;
    bi.pszDisplayName = buf;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    bool ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) path = buf;
    return ok;
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
void loadPreview(const fs::path& p) {
    Image im;
    std::string err;
    g.pvHas = false;
    if (!loadImageFile(p, im, err)) {
        g.pvText = L"Cannot preview this file.\n" + W(err);
    } else {
        const size_t stride = ((size_t)im.w * 3 + 3) & ~(size_t)3;
        g.pvDib.assign(stride * (size_t)im.h, 0);
        for (int y = 0; y < im.h; y++) {
            const uint8_t* s = im.rgb.data() + (size_t)y * im.w * 3;
            uint8_t* d = g.pvDib.data() + (size_t)y * stride;
            for (int x = 0; x < im.w; x++) {
                d[x * 3 + 0] = s[x * 3 + 2];
                d[x * 3 + 1] = s[x * 3 + 1];
                d[x * 3 + 2] = s[x * 3 + 0];
            }
        }
        g.pvW = im.w;
        g.pvH = im.h;
        g.pvHas = true;
        g.pvText = p.filename().wstring() + L"   " + std::to_wstring(im.w) + L" × " + std::to_wstring(im.h);
    }
    InvalidateRect(g.preview, nullptr, TRUE);
}

LRESULT CALLBACK previewProc(HWND hw, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hw, &ps);
            RECT rc;
            GetClientRect(hw, &rc);
            HBRUSH br = CreateSolidBrush(RGB(30, 30, 34));
            FillRect(dc, &rc, br);
            DeleteObject(br);
            HGDIOBJ old = SelectObject(dc, g.font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(205, 205, 215));
            const int capH = S(22), pad = S(6);
            if (g.pvHas) {
                int aw = rc.right - 2 * pad, ah = rc.bottom - capH - 2 * pad;
                if (aw > 0 && ah > 0) {
                    double sc = std::min((double)aw / g.pvW, (double)ah / g.pvH);
                    sc = std::min(sc, 8.0);
                    int dw = std::max(1, (int)(g.pvW * sc)), dh = std::max(1, (int)(g.pvH * sc));
                    int dx = (rc.right - dw) / 2, dy = pad + (ah - dh) / 2;
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = g.pvW;
                    bmi.bmiHeader.biHeight = -g.pvH;  // top-down
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 24;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    SetStretchBltMode(dc, sc < 1.0 ? HALFTONE : COLORONCOLOR);
                    SetBrushOrgEx(dc, 0, 0, nullptr);
                    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, g.pvW, g.pvH, g.pvDib.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
                }
                RECT cr = {pad, rc.bottom - capH, rc.right - pad, rc.bottom};
                DrawTextW(dc, g.pvText.c_str(), -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
            } else {
                std::wstring t = g.pvText.empty() ? L"Preview\n\nSelect an image from the list" : g.pvText;
                RECT cr = {pad, pad, rc.right - pad, rc.bottom - pad};
                DrawTextW(dc, t.c_str(), -1, &cr, DT_CENTER | DT_WORDBREAK | DT_VCENTER | DT_NOCLIP);
            }
            SelectObject(dc, old);
            EndPaint(hw, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hw, m, wp, lp);
}

// ---------------------------------------------------------------------------
// Helpers for the UI state
// ---------------------------------------------------------------------------
Format currentFormat() {
    if (SendMessageW(g.rbOP, BM_GETCHECK, 0, 0) == BST_CHECKED) return Format::OPlus;
    if (SendMessageW(g.rbQP, BM_GETCHECK, 0, 0) == BST_CHECKED) return Format::QualcommPadded;
    return Format::Qualcomm;
}

void setFormat(Format f) {
    SendMessageW(g.rbQC, BM_SETCHECK, f == Format::Qualcomm ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.rbQP, BM_SETCHECK, f == Format::QualcommPadded ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.rbOP, BM_SETCHECK, f == Format::OPlus ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g.chkRle, !g.busy && f != Format::OPlus);
}

void fillList(const std::vector<fs::path>& files) {
    g.listPaths = files;
    SendMessageW(g.list, LB_RESETCONTENT, 0, 0);
    for (auto& p : files) SendMessageW(g.list, LB_ADDSTRING, 0, (LPARAM)p.filename().wstring().c_str());
    g.pvHas = false;
    g.pvText.clear();
    InvalidateRect(g.preview, nullptr, TRUE);
}

void selectFirst() {
    if (g.listPaths.empty()) return;
    SendMessageW(g.list, LB_SETCURSEL, 0, 0);
    loadPreview(g.listPaths[0]);
}

void refreshRepackList() {
    std::wstring dir = trimPath(getText(g.eRIn));
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(fs::path(dir), ec)) return;
    fillList(listImageFiles(fs::path(dir)));
    selectFirst();
}

void setBusy(bool b) {
    g.busy = b;
    HWND all[] = {g.rbQC, g.rbQP, g.rbOP, g.eUIn, g.bUIn, g.eUOut, g.bUOut, g.bUnpack, g.eRIn, g.bRIn, g.eROut, g.bROut, g.bRepack};
    for (HWND h : all) EnableWindow(h, !b);
    EnableWindow(g.chkRle, !b && currentFormat() != Format::OPlus);
    if (b) {
        EnableWindow(g.bOpenOut, FALSE);
        EnableWindow(g.bShowImg, FALSE);
    }
}

void setUnpackInput(const fs::path& p) {
    SetWindowTextW(g.eUIn, p.wstring().c_str());
    SetWindowTextW(g.eUOut, (p.parent_path() / (p.stem().wstring() + L"_unpacked")).wstring().c_str());
    Format f;
    if (detectFormat(p, f)) {
        setFormat(f);
        logLocal(LogLevel::Info, std::wstring(L"Detected format: ") + W(formatName(f)));
    } else {
        logLocal(LogLevel::Warn, L"Splash magic not recognised - choose the device format manually.");
    }
}

void setRepackInput(const fs::path& dir) {
    SetWindowTextW(g.eRIn, dir.wstring().c_str());
    fs::path name = dir.filename().empty() ? dir.parent_path().filename() : dir.filename();
    SetWindowTextW(g.eROut, (dir.parent_path() / (name.wstring() + L"_repacked.img")).wstring().c_str());
    std::error_code ec;
    if (fs::exists(dir / ".metadata", ec)) {
        setFormat(Format::OPlus);
        logLocal(LogLevel::Info, L".metadata found - switched to OPlus format.");
    }
    refreshRepackList();
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
DWORD WINAPI jobProc(LPVOID param) {
    std::unique_ptr<Job> job((Job*)param);
    HWND hwnd = job->hwnd;
    Callbacks cb;
    cb.log = [hwnd](LogLevel l, const std::string& s) {
        PostMessageW(hwnd, WM_APP_LOG, (WPARAM)l, (LPARAM) new std::string(s));
    };
    cb.progress = [hwnd](int done, int total) { PostMessageW(hwnd, WM_APP_PROGRESS, (WPARAM)done, (LPARAM)total); };

    auto* done = new DoneMsg{false, job->unpack, job->out, {}};
    try {
        if (job->unpack) {
            done->ok = unpackSplash(job->fmt, job->in, job->out, cb, done->files);
        } else {
            done->ok = repackSplash(job->fmt, job->in, job->out, job->opt, cb);
        }
    } catch (const std::exception& e) {
        cb.error(std::string("Unexpected error: ") + e.what());
        done->ok = false;
    } catch (...) {
        cb.error("Unexpected error.");
        done->ok = false;
    }
    PostMessageW(hwnd, WM_APP_DONE, 0, (LPARAM)done);
    return 0;
}

void launch(Job* job) {
    setBusy(true);
    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
    HANDLE h = CreateThread(nullptr, 0, jobProc, job, 0, nullptr);
    if (!h) {
        delete job;
        setBusy(false);
        msg(L"Could not start the worker thread.", MB_ICONERROR);
        return;
    }
    CloseHandle(h);
}

void startUnpack() {
    std::wstring in = trimPath(getText(g.eUIn)), out = trimPath(getText(g.eUOut));
    std::error_code ec;
    fs::path pin(in);
    if (in.empty() || !fs::is_regular_file(pin, ec)) {
        msg(L"Please choose an existing splash image to unpack.", MB_ICONWARNING);
        return;
    }
    if (out.empty()) {
        out = (pin.parent_path() / (pin.stem().wstring() + L"_unpacked")).wstring();
        SetWindowTextW(g.eUOut, out.c_str());
    }
    Format fmt = currentFormat(), det;
    if (detectFormat(pin, det) && det != fmt) {
        std::wstring q = std::wstring(L"This file looks like a ") + W(formatName(det)) + L" image, but " +
                         W(formatName(fmt)) + L" is selected.\n\nUse the detected format?";
        int r = MessageBoxW(g.hwnd, q.c_str(), kAppTitle, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) {
            fmt = det;
            setFormat(fmt);
        }
    }
    fs::path pout(out);
    if (fs::is_directory(pout, ec) && fs::directory_iterator(pout, ec) != fs::directory_iterator()) {
        if (MessageBoxW(g.hwnd, L"The output folder is not empty. Files with the same names will be overwritten.\n\nContinue?",
                        kAppTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    logLocal(LogLevel::Info, L"--------------------------------------------");
    fillList({});
    auto* job = new Job{g.hwnd, true, fmt, pin, pout, RepackOptions()};
    launch(job);
}

void startRepack() {
    std::wstring in = trimPath(getText(g.eRIn)), out = trimPath(getText(g.eROut));
    std::error_code ec;
    fs::path pin(in);
    if (in.empty() || !fs::is_directory(pin, ec)) {
        msg(L"Please choose the folder that contains the images.", MB_ICONWARNING);
        return;
    }
    Format fmt = currentFormat();
    if (fmt == Format::OPlus && !fs::exists(pin / ".metadata", ec)) {
        msg(L"OPlus repacking needs the \".metadata\" file that is created when you unpack an OPlus splash image.\n"
            L"Unpack the original image first and put your edited images into that folder.",
            MB_ICONWARNING);
        return;
    }
    if (out.empty()) {
        fs::path name = pin.filename().empty() ? pin.parent_path().filename() : pin.filename();
        out = (pin.parent_path() / (name.wstring() + L"_repacked.img")).wstring();
        SetWindowTextW(g.eROut, out.c_str());
    }
    fs::path pout(out);
    if (fs::exists(pout, ec)) {
        if (!fs::is_regular_file(pout, ec)) {
            msg(L"The output path exists and is not a file.", MB_ICONWARNING);
            return;
        }
        if (MessageBoxW(g.hwnd, L"The output file already exists. Overwrite it?", kAppTitle, MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    RepackOptions opt;
    opt.qualcommRle = SendMessageW(g.chkRle, BM_GETCHECK, 0, 0) == BST_CHECKED;
    logLocal(LogLevel::Info, L"--------------------------------------------");
    refreshRepackList();
    auto* job = new Job{g.hwnd, false, fmt, pin, pout, opt};
    launch(job);
}

// ---------------------------------------------------------------------------
// Window construction & layout
// ---------------------------------------------------------------------------
HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.hwnd,
                           (HMENU)(INT_PTR)id, g.hInst, nullptr);
}

BOOL CALLBACK setFontProc(HWND h, LPARAM) {
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    return TRUE;
}

void createControls() {
    const DWORD TS = WS_TABSTOP;
    const DWORD EDIT = ES_AUTOHSCROLL;
    // group boxes first so that they stay behind the controls
    g.grpFmt = mk(L"BUTTON", L" 1. Device format ", BS_GROUPBOX, 0);
    g.grpUnpack = mk(L"BUTTON", L" 2. Unpack  (splash.img → images) ", BS_GROUPBOX, 0);
    g.grpRepack = mk(L"BUTTON", L" 3. Repack  (images → splash.img) ", BS_GROUPBOX, 0);
    g.grpImages = mk(L"BUTTON", L" Images ", BS_GROUPBOX, 0);

    g.rbQC = mk(L"BUTTON", L"Qualcomm  (SPLASH!!)", BS_AUTORADIOBUTTON | WS_GROUP | TS, ID_RB_QC);
    g.rbQP = mk(L"BUTTON", L"Qualcomm, padded  (Lenovo etc.)", BS_AUTORADIOBUTTON | TS, ID_RB_QP);
    g.rbOP = mk(L"BUTTON", L"OPlus: OPPO / OnePlus / Realme", BS_AUTORADIOBUTTON | TS, ID_RB_OP);

    g.lblUIn = mk(L"STATIC", L"Splash image to unpack:", 0, 0);
    g.eUIn = mk(L"EDIT", L"", EDIT | TS, ID_IN_UNPACK, WS_EX_CLIENTEDGE);
    g.bUIn = mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON | TS, ID_BR_UNPACK_IN);
    g.lblUOut = mk(L"STATIC", L"Output folder:", 0, 0);
    g.eUOut = mk(L"EDIT", L"", EDIT | TS, ID_OUT_UNPACK, WS_EX_CLIENTEDGE);
    g.bUOut = mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON | TS, ID_BR_UNPACK_OUT);
    g.bOpenOut = mk(L"BUTTON", L"Open folder", BS_PUSHBUTTON | TS, ID_BTN_OPEN_OUT);
    g.bUnpack = mk(L"BUTTON", L"Unpack", BS_DEFPUSHBUTTON | TS, ID_BTN_UNPACK);

    g.lblRIn = mk(L"STATIC", L"Folder with images:", 0, 0);
    g.eRIn = mk(L"EDIT", L"", EDIT | TS, ID_IN_REPACK, WS_EX_CLIENTEDGE);
    g.bRIn = mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON | TS, ID_BR_REPACK_IN);
    g.lblROut = mk(L"STATIC", L"Output splash image:", 0, 0);
    g.eROut = mk(L"EDIT", L"", EDIT | TS, ID_OUT_REPACK, WS_EX_CLIENTEDGE);
    g.bROut = mk(L"BUTTON", L"Browse...", BS_PUSHBUTTON | TS, ID_BR_REPACK_OUT);
    g.chkRle = mk(L"BUTTON", L"Qualcomm: compress with RLE24", BS_AUTOCHECKBOX | TS, ID_CHK_RLE);
    g.bShowImg = mk(L"BUTTON", L"Show file", BS_PUSHBUTTON | TS, ID_BTN_SHOW_IMG);
    g.bRepack = mk(L"BUTTON", L"Repack", BS_PUSHBUTTON | TS, ID_BTN_REPACK);

    g.list = mk(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | TS, ID_LIST, WS_EX_CLIENTEDGE);
    g.preview = CreateWindowExW(WS_EX_CLIENTEDGE, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g.hwnd, nullptr,
                                g.hInst, nullptr);

    g.log = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | TS, ID_LOG, WS_EX_CLIENTEDGE);
    g.progress = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, ID_PROGRESS);
    g.bClear = mk(L"BUTTON", L"Clear log", BS_PUSHBUTTON | TS, ID_BTN_CLEAR);
    g.bAbout = mk(L"BUTTON", L"About", BS_PUSHBUTTON | TS, ID_BTN_ABOUT);

    EnumChildWindows(g.hwnd, setFontProc, 0);
    SendMessageW(g.log, WM_SETFONT, (WPARAM)g.mono, TRUE);
    SendMessageW(g.log, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0);
    SendMessageW(g.progress, PBM_SETRANGE32, 0, 100);

    SendMessageW(g.chkRle, BM_SETCHECK, BST_CHECKED, 0);
    setFormat(Format::Qualcomm);
    EnableWindow(g.bOpenOut, FALSE);
    EnableWindow(g.bShowImg, FALSE);
    DragAcceptFiles(g.hwnd, TRUE);
}

void mv(HWND h, int x, int y, int w, int hgt) { MoveWindow(h, x, y, w, hgt, TRUE); }

void layout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int Wd = rc.right, Ht = rc.bottom;
    const int M = S(8), leftW = S(440), topH = S(416), in = S(12);
    const int btnW = S(84), editH = S(23), rowGap = S(6);

    // --- format group
    int x = M, y = M;
    mv(g.grpFmt, x, y, leftW, S(76));
    mv(g.rbQC, x + in, y + S(20), S(160), S(22));
    mv(g.rbQP, x + in + S(166), y + S(20), leftW - in * 2 - S(166), S(22));
    mv(g.rbOP, x + in, y + S(44), leftW - in * 2, S(22));

    auto block = [&](int gy, int gh, HWND grp, HWND l1, HWND e1, HWND b1, HWND l2, HWND e2, HWND b2) {
        mv(grp, x, gy, leftW, gh);
        int ew = leftW - in * 2 - btnW - rowGap;
        mv(l1, x + in, gy + S(20), leftW - in * 2, S(16));
        mv(e1, x + in, gy + S(38), ew, editH);
        mv(b1, x + in + ew + rowGap, gy + S(37), btnW, S(25));
        mv(l2, x + in, gy + S(68), leftW - in * 2, S(16));
        mv(e2, x + in, gy + S(86), ew, editH);
        mv(b2, x + in + ew + rowGap, gy + S(85), btnW, S(25));
    };

    int gy1 = y + S(84), gh = S(152);
    block(gy1, gh, g.grpUnpack, g.lblUIn, g.eUIn, g.bUIn, g.lblUOut, g.eUOut, g.bUOut);
    mv(g.bUnpack, x + leftW - in - S(110), gy1 + S(116), S(110), S(28));
    mv(g.bOpenOut, x + leftW - in - S(110) - rowGap - S(110), gy1 + S(116), S(110), S(28));

    int gy2 = gy1 + gh + S(8);
    block(gy2, gh, g.grpRepack, g.lblRIn, g.eRIn, g.bRIn, g.lblROut, g.eROut, g.bROut);
    mv(g.chkRle, x + in, gy2 + S(120), leftW - in * 2 - S(110) * 2 - rowGap * 2, S(22));
    mv(g.bRepack, x + leftW - in - S(110), gy2 + S(116), S(110), S(28));
    mv(g.bShowImg, x + leftW - in - S(110) - rowGap - S(110), gy2 + S(116), S(110), S(28));

    // --- images group (right side)
    int rx = M + leftW + M, rw = Wd - rx - M, rh = topH - M;
    mv(g.grpImages, rx, M, rw, rh);
    int listW = S(170);
    mv(g.list, rx + S(10), M + S(20), listW, rh - S(30));
    mv(g.preview, rx + S(10) + listW + S(6), M + S(20), rw - S(20) - listW - S(6), rh - S(30));

    // --- bottom: log + progress
    int rowH = S(26);
    int bottomY = Ht - M - rowH;
    int logY = topH + S(2);
    mv(g.log, M, logY, Wd - 2 * M, bottomY - logY - M);
    int bw = S(84);
    mv(g.progress, M, bottomY + S(4), Wd - 2 * M - bw * 2 - rowGap * 2 - S(8), S(18));
    mv(g.bClear, Wd - M - bw * 2 - rowGap, bottomY, bw, rowH);
    mv(g.bAbout, Wd - M - bw, bottomY, bw, rowH);
}

void showAbout() {
    MessageBoxW(g.hwnd,
                L"Magic Splash Wand — C++ GUI\n\n"
                L"Unpack and repack the splash (boot logo) image of Qualcomm devices:\n"
                L"  • Qualcomm  \"SPLASH!!\"      (raw / RLE24), classic + padded (Lenovo etc.)\n"
                L"  • OPlus     \"SPLASH LOGO!\"  (BMP, optional GZIP)\n\n"
                L"Based on the PHP tool \"Magic Splash Wand\" 1.2 by\n"
                L"NekoYuzu (MlgmXyysd), MeowCat Studio and Meow Mobile\n"
                L"https://github.com/MlgmXyysd/Magic-Splash-Wand\n\n"
                L"Tips:\n"
                L"  • Drag and drop a splash image or an image folder onto the window.\n"
                L"  • Qualcomm images are packed in natural file-name order (0.png, 1.png, ... 10.png).\n"
                L"  • Keep every image at its original resolution and keep RLE24 on (raw makes the file huge).\n"
                L"  • For OPlus keep the \".metadata\" file next to the unpacked .bmp files.\n"
                L"  • Always keep a backup of your original splash partition.",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
}

void openFolder(const fs::path& p) { ShellExecuteW(g.hwnd, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void showInFolder(const fs::path& p) {
    std::wstring args = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(g.hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void onDrop(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0, nullptr, 0);
    if (n > 0 && !g.busy) {
        std::wstring path(n + 1, L'\0');
        DragQueryFileW(drop, 0, &path[0], n + 1);
        path.resize(n);
        std::error_code ec;
        fs::path p(path);
        if (fs::is_directory(p, ec)) setRepackInput(p);
        else if (fs::is_regular_file(p, ec)) setUnpackInput(p);
    }
    DragFinish(drop);
}

LRESULT CALLBACK wndProc(HWND hw, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_CREATE:
            g.hwnd = hw;
            createControls();
            return 0;

        case WM_SIZE:
            if (wp != SIZE_MINIMIZED && g.log) layout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mm = (MINMAXINFO*)lp;
            RECT r = {0, 0, S(930), S(650)};
            AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(hw, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(hw, GWL_EXSTYLE));
            mm->ptMinTrackSize.x = r.right - r.left;
            mm->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }

        case WM_DROPFILES:
            onDrop((HDROP)wp);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            std::wstring s;
            switch (id) {
                case ID_RB_QC: setFormat(Format::Qualcomm); break;
                case ID_RB_QP: setFormat(Format::QualcommPadded); break;
                case ID_RB_OP: setFormat(Format::OPlus); break;
                case ID_BR_UNPACK_IN:
                    if (browseOpenFile(s)) setUnpackInput(fs::path(s));
                    break;
                case ID_BR_UNPACK_OUT:
                    if (browseFolder(L"Select the output folder", s)) SetWindowTextW(g.eUOut, s.c_str());
                    break;
                case ID_BR_REPACK_IN:
                    if (browseFolder(L"Select the folder that contains the images", s)) setRepackInput(fs::path(s));
                    break;
                case ID_BR_REPACK_OUT:
                    if (browseSaveFile(s)) SetWindowTextW(g.eROut, s.c_str());
                    break;
                case ID_IN_REPACK:
                    if (code == EN_KILLFOCUS) refreshRepackList();
                    break;
                case ID_BTN_UNPACK: startUnpack(); break;
                case ID_BTN_REPACK: startRepack(); break;
                case ID_BTN_OPEN_OUT: openFolder(g.lastUnpackDir); break;
                case ID_BTN_SHOW_IMG: showInFolder(g.lastRepackFile); break;
                case ID_BTN_CLEAR: SetWindowTextW(g.log, L""); break;
                case ID_BTN_ABOUT: showAbout(); break;
                case ID_LIST:
                    if (code == LBN_SELCHANGE) {
                        LRESULT i = SendMessageW(g.list, LB_GETCURSEL, 0, 0);
                        if (i >= 0 && (size_t)i < g.listPaths.size()) loadPreview(g.listPaths[(size_t)i]);
                    } else if (code == LBN_DBLCLK) {
                        LRESULT i = SendMessageW(g.list, LB_GETCURSEL, 0, 0);
                        if (i >= 0 && (size_t)i < g.listPaths.size())
                            ShellExecuteW(hw, L"open", g.listPaths[(size_t)i].wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    break;
            }
            return 0;
        }

        case WM_APP_LOG: {
            std::unique_ptr<std::string> text((std::string*)lp);
            appendLog((LogLevel)wp, *text);
            return 0;
        }

        case WM_APP_PROGRESS:
            if (lp > 0) SendMessageW(g.progress, PBM_SETPOS, (WPARAM)((long long)wp * 100 / (long long)lp), 0);
            return 0;

        case WM_APP_DONE: {
            std::unique_ptr<DoneMsg> d((DoneMsg*)lp);
            setBusy(false);
            SendMessageW(g.progress, PBM_SETPOS, d->ok ? 100 : 0, 0);
            if (d->ok && d->unpack) {
                g.lastUnpackDir = d->out;
                fillList(d->files);
                selectFirst();
                EnableWindow(g.bOpenOut, TRUE);
            } else if (d->ok) {
                g.lastRepackFile = d->out;
                EnableWindow(g.bShowImg, TRUE);
            } else {
                msg(L"The operation failed. See the log for details.", MB_ICONERROR);
            }
            return 0;
        }

        case WM_CLOSE:
            if (g.busy) {
                msg(L"An operation is still running. Please wait until it finishes.", MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hw);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hw, m, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    g.hInst = hInst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    if (!initImaging()) MessageBoxW(nullptr, L"GDI+ could not be started - only BMP/PNG preview may fail.", kAppTitle, MB_ICONWARNING);

    HDC dc = GetDC(nullptr);
    g.dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    g.mono = CreateFontW(-MulDiv(9, g.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    HICON icon = LoadIconW(hInst, MAKEINTRESOURCEW(1));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kMainClass;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    RegisterClassExW(&wc);

    WNDCLASSW pc{};
    pc.lpfnWndProc = previewProc;
    pc.hInstance = hInst;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.lpszClassName = kPreviewClass;
    RegisterClassW(&pc);

    RECT r = {0, 0, S(1000), S(720)};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectEx(&r, style, FALSE, WS_EX_CONTROLPARENT);
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kMainClass, L"Magic Splash Wand  —  splash image unpack / repack",
                                style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hInst,
                                nullptr);
    if (!hwnd) return 1;
    layout();
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    appendLog(LogLevel::Info, "Ready. Choose a splash image to unpack, or a folder of images to repack (drag and drop works too).");

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    DeleteObject(g.font);
    DeleteObject(g.mono);
    shutdownImaging();
    CoUninitialize();
    return (int)m.wParam;
}

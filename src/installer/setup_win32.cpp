#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <msi.h>

#include "../core/version.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int IDR_PAYLOAD_EXE = 101;
constexpr UINT WM_SETUP_STEP = WM_APP + 10;
constexpr UINT WM_SETUP_DONE = WM_APP + 11;

constexpr int ID_BTN_PRIMARY = 1001;
constexpr int ID_BTN_CANCEL = 1002;
constexpr int ID_BTN_BROWSE = 1003;
constexpr int ID_BTN_LANG = 1004;
constexpr int ID_EDIT_DIR = 1005;
constexpr int ID_CHK_STARTMENU = 1006;
constexpr int ID_CHK_DESKTOP = 1007;
constexpr int ID_CHK_AUTOSTART = 1008;
constexpr int ID_CHK_LAUNCH = 1009;
constexpr int ID_PROGRESS = 1010;
constexpr int ID_LBL_STATUS = 1011;

constexpr const wchar_t* UNINSTALL_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TypeCount";
constexpr const wchar_t* RUN_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

struct ExistingInstall {
    bool found = false;
    bool is_msi = false;
    std::wstring msi_product_code;
    std::wstring version;
    std::wstring install_dir;
};

struct WizardState {
    HINSTANCE hinst = nullptr;
    HWND hwnd = nullptr;
    HFONT font_ui = nullptr;
    HFONT font_title = nullptr;
    HFONT font_sub = nullptr;
    HBRUSH brush_white = nullptr;
    HBRUSH brush_bg = nullptr;
    HICON icon = nullptr;

    bool fa = false;
    int step = 0; // 0 = welcome/options, 1 = installing, 2 = complete
    bool busy = false;
    bool install_ok = false;
    std::wstring error_msg;

    ExistingInstall existing;
    std::wstring target_dir;
    bool opt_startmenu = true;
    bool opt_desktop = true;
    bool opt_autostart = true;
    bool opt_launch = true;

    // Controls
    HWND lbl_title = nullptr;
    HWND lbl_sub = nullptr;
    HWND btn_lang = nullptr;
    HWND lbl_detected = nullptr;
    HWND lbl_dir = nullptr;
    HWND edit_dir = nullptr;
    HWND btn_browse = nullptr;
    HWND chk_startmenu = nullptr;
    HWND chk_desktop = nullptr;
    HWND chk_autostart = nullptr;
    HWND progress = nullptr;
    HWND lbl_status = nullptr;
    HWND lbl_finish_msg = nullptr;
    HWND chk_launch = nullptr;
    HWND btn_primary = nullptr;
    HWND btn_cancel = nullptr;
};

static std::wstring known_folder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) {
        out = p;
        CoTaskMemFree(p);
    }
    return out;
}

static std::wstring default_install_dir() {
    std::wstring local = known_folder(FOLDERID_LocalAppData);
    if (!local.empty()) return local + L"\\Programs\\TypeCount";
    return L"C:\\TypeCount";
}

static std::wstring exe_path_self() {
    std::wstring buf(1024, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return buf;
}

static std::wstring read_exe_version_str(const std::wstring& path) {
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (sz == 0) return {};
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return {};
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) || !ffi) return {};
    return std::to_wstring(HIWORD(ffi->dwFileVersionMS)) + L"." +
           std::to_wstring(LOWORD(ffi->dwFileVersionMS)) + L"." +
           std::to_wstring(HIWORD(ffi->dwFileVersionLS));
}

static ExistingInstall detect_existing_installation() {
    ExistingInstall ex;
    wchar_t prod[64]{};
    if (MsiEnumRelatedProductsW(TC_UPGRADE_GUID_W, 0, 0, prod) == ERROR_SUCCESS && prod[0] != L'\0') {
        ex.found = true;
        ex.is_msi = true;
        ex.msi_product_code = prod;
        wchar_t ver[64]{};
        DWORD vsz = 64;
        if (MsiGetProductInfoW(prod, L"VersionString", ver, &vsz) == ERROR_SUCCESS) {
            ex.version = ver;
        }
        wchar_t loc[1024]{};
        DWORD lsz = 1024;
        if (MsiGetProductInfoW(prod, L"InstallLocation", loc, &lsz) == ERROR_SUCCESS && loc[0] != L'\0') {
            ex.install_dir = loc;
        }
    }

    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (HKEY root : roots) {
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, UNINSTALL_SUBKEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t val[1024]{};
            DWORD type = 0, sz = sizeof(val);
            if (RegQueryValueExW(k, L"InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(val), &sz) == ERROR_SUCCESS && val[0] != L'\0') {
                ex.found = true;
                if (ex.install_dir.empty()) ex.install_dir = val;
            }
            sz = sizeof(val);
            if (RegQueryValueExW(k, L"DisplayVersion", nullptr, &type, reinterpret_cast<BYTE*>(val), &sz) == ERROR_SUCCESS && val[0] != L'\0') {
                if (ex.version.empty()) ex.version = val;
            }
            RegCloseKey(k);
        }
    }

    if (ex.install_dir.empty()) {
        std::wstring cand = default_install_dir();
        if (std::filesystem::exists(std::filesystem::path(cand + L"\\TypeCount.exe"))) {
            ex.found = true;
            ex.install_dir = cand;
        }
    }
    if (ex.found && ex.version.empty() && !ex.install_dir.empty()) {
        ex.version = read_exe_version_str(ex.install_dir + L"\\TypeCount.exe");
    }
    return ex;
}

static void stop_running_typecount() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HWND host = FindWindowW(L"TypeCountCaptureHost", nullptr);
        if (!host) break;
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(host, &pid);
        HANDLE proc = pid ? OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid) : nullptr;
        PostMessageW(host, WM_APP + 4, 0, 0);
        if (proc && WaitForSingleObject(proc, 400) != WAIT_OBJECT_0) {
            if (tid) PostThreadMessageW(tid, WM_QUIT, 0, 0);
            if (WaitForSingleObject(proc, 400) != WAIT_OBJECT_0) {
                TerminateProcess(proc, 0);
                WaitForSingleObject(proc, 2000);
            }
        }
        if (proc) CloseHandle(proc);
        Sleep(120);
    }
}

static bool create_shortcut(const std::wstring& lnk_path, const std::wstring& target_exe, const std::wstring& work_dir, const std::wstring& desc) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (FAILED(hr) || !psl) return false;
    psl->SetPath(target_exe.c_str());
    psl->SetWorkingDirectory(work_dir.c_str());
    psl->SetDescription(desc.c_str());
    psl->SetIconLocation(target_exe.c_str(), 0);
    IPersistFile* ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
    bool ok = false;
    if (SUCCEEDED(hr) && ppf) {
        ok = SUCCEEDED(ppf->Save(lnk_path.c_str(), TRUE));
        ppf->Release();
    }
    psl->Release();
    return ok;
}

static bool write_embedded_payload(const std::wstring& dst_exe, std::wstring& err) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAYLOAD_EXE), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) { err = L"Missing installer payload resource."; return false; }
    DWORD sz = SizeofResource(nullptr, res);
    HGLOBAL hg = LoadResource(nullptr, res);
    const void* ptr = hg ? LockResource(hg) : nullptr;
    if (!ptr || sz == 0) { err = L"Unable to read embedded application binary."; return false; }

    HANDLE fh = CreateFileW(dst_exe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        err = L"Cannot write to target folder (try choosing a writable folder or closing TypeCount).";
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(fh, ptr, sz, &written, nullptr) && written == sz;
    CloseHandle(fh);
    if (!ok) {
        err = L"Disk write failed while installing TypeCount.exe.";
        return false;
    }
    return true;
}

static void register_uninstall_entry(const std::wstring& install_dir, const std::wstring& exe_path, const std::wstring& uninst_path, DWORD size_kb) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, UNINSTALL_SUBKEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    auto set_sz = [&](const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    };
    auto set_dword = [&](const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    };
    set_sz(L"DisplayName", L"TypeCount");
    set_sz(L"DisplayVersion", TC_VERSION_WSTR);
    set_sz(L"Publisher", L"TypeCount");
    set_sz(L"DisplayIcon", exe_path + L",0");
    set_sz(L"InstallLocation", install_dir);
    set_sz(L"UninstallString", L"\"" + uninst_path + L"\" --uninstall");
    set_sz(L"QuietUninstallString", L"\"" + uninst_path + L"\" --uninstall --silent");
    set_sz(L"URLInfoAbout", L"https://github.com/Amirmpm/typeCount");
    set_dword(L"EstimatedSize", std::max<DWORD>(1024, size_kb));
    set_dword(L"NoModify", 1);
    set_dword(L"NoRepair", 1);
    set_dword(L"VersionMajor", TC_VERSION_MAJOR);
    set_dword(L"VersionMinor", TC_VERSION_MINOR);
    RegCloseKey(k);
}

static void set_run_autostart(const std::wstring& exe_path, bool enable) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_SUBKEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (enable) {
        std::wstring cmd = L"\"" + exe_path + L"\" --background";
        RegSetValueExW(k, L"TypeCount", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"TypeCount");
    }
    RegCloseKey(k);
}

static void post_progress(HWND hwnd, int pct, const wchar_t* msg) {
    if (!hwnd) return;
    auto* txt = new std::wstring(msg);
    PostMessageW(hwnd, WM_SETUP_STEP, static_cast<WPARAM>(pct), reinterpret_cast<LPARAM>(txt));
}

static bool perform_install(WizardState* st) {
    HWND hwnd = st->hwnd;
    bool fa = st->fa;

    post_progress(hwnd, 12, fa ? L"در حال بستن نسخه در حال اجرا…" : L"Closing any running TypeCount instance…");
    stop_running_typecount();

    if (st->existing.is_msi && !st->existing.msi_product_code.empty()) {
        post_progress(hwnd, 24, fa ? L"در حال ارتقای بسته MSI قبلی…" : L"Upgrading previous MSI package cleanly…");
        std::wstring cmd = L"msiexec.exe /x " + st->existing.msi_product_code + L" /qn /norestart";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 15000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    post_progress(hwnd, 42, fa ? L"در حال ساخت پوشه برنامه…" : L"Preparing installation directory…");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(st->target_dir), ec);

    std::wstring exe_dst = st->target_dir + L"\\TypeCount.exe";
    std::wstring uninst_dst = st->target_dir + L"\\TypeCount-Uninstaller.exe";

    post_progress(hwnd, 62, fa ? L"در حال کپی فایل‌های TypeCount…" : L"Installing TypeCount files…");
    std::wstring err;
    if (!write_embedded_payload(exe_dst, err)) {
        st->error_msg = err;
        return false;
    }

    std::wstring self = exe_path_self();
    if (_wcsicmp(self.c_str(), uninst_dst.c_str()) != 0) {
        CopyFileW(self.c_str(), uninst_dst.c_str(), FALSE);
    }

    post_progress(hwnd, 78, fa ? L"در حال ایجاد میان‌برها و ثبت در ویندوز…" : L"Creating shortcuts & registering with Windows…");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring start_dir = known_folder(FOLDERID_Programs);
    if (st->opt_startmenu && !start_dir.empty()) {
        std::wstring menu_folder = start_dir + L"\\TypeCount";
        std::filesystem::create_directories(std::filesystem::path(menu_folder), ec);
        create_shortcut(menu_folder + L"\\TypeCount.lnk", exe_dst, st->target_dir, L"TypeCount · Keyboard & Mouse Statistics");
        create_shortcut(menu_folder + L"\\Uninstall TypeCount.lnk", uninst_dst, st->target_dir, L"Uninstall TypeCount");
    }
    std::wstring desk_dir = known_folder(FOLDERID_Desktop);
    if (st->opt_desktop && !desk_dir.empty()) {
        create_shortcut(desk_dir + L"\\TypeCount.lnk", exe_dst, st->target_dir, L"TypeCount · Keyboard & Mouse Statistics");
    }
    CoUninitialize();

    uint64_t fbytes = std::filesystem::file_size(std::filesystem::path(exe_dst), ec);
    register_uninstall_entry(st->target_dir, exe_dst, uninst_dst, static_cast<DWORD>(fbytes / 1024));
    if (st->opt_autostart) {
        set_run_autostart(exe_dst, true);
    }

    post_progress(hwnd, 100, fa ? L"نصب با موفقیت کامل شد!" : L"Installation complete!");
    return true;
}

static int perform_uninstall(bool silent) {
    bool fa = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PERSIAN);
    if (!silent) {
        int rc = MessageBoxW(
            nullptr,
            fa ? L"آیا مایل به حذف TypeCount از سیستم هستید؟\n(تاریخچه و آمار شما در پوشه کاربری حفظ می‌شود.)"
               : L"Uninstall TypeCount from this PC?\n(Your recorded history in LocalAppData will be preserved.)",
            L"TypeCount",
            MB_YESNO | MB_ICONQUESTION);
        if (rc != IDYES) return 0;
    }

    stop_running_typecount();

    ExistingInstall ex = detect_existing_installation();
    std::wstring dir = ex.install_dir;
    if (dir.empty()) dir = std::filesystem::path(exe_path_self()).parent_path().wstring();

    std::error_code ec;
    std::wstring start_dir = known_folder(FOLDERID_Programs);
    if (!start_dir.empty()) std::filesystem::remove_all(std::filesystem::path(start_dir + L"\\TypeCount"), ec);
    std::wstring desk_dir = known_folder(FOLDERID_Desktop);
    if (!desk_dir.empty()) std::filesystem::remove(std::filesystem::path(desk_dir + L"\\TypeCount.lnk"), ec);

    RegDeleteKeyW(HKEY_CURRENT_USER, UNINSTALL_SUBKEY);
    HKEY rk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_SUBKEY, 0, KEY_SET_VALUE, &rk) == ERROR_SUCCESS) {
        RegDeleteValueW(rk, L"TypeCount");
        RegCloseKey(rk);
    }

    if (!dir.empty()) {
        std::filesystem::remove(std::filesystem::path(dir + L"\\TypeCount.exe"), ec);
        std::wstring self = exe_path_self();
        std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 2 > nul & del /f /q \"" + self + L"\" & rmdir \"" + dir + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    if (!silent) {
        MessageBoxW(
            nullptr,
            fa ? L"TypeCount با موفقیت حذف شد." : L"TypeCount was uninstalled successfully.",
            L"TypeCount",
            MB_OK | MB_ICONINFORMATION);
    }
    return 0;
}

static void apply_texts_and_visibility(WizardState* st) {
    bool fa = st->fa;
    LONG_PTR ex = GetWindowLongPtrW(st->hwnd, GWL_EXSTYLE);
    if (fa) ex |= WS_EX_LAYOUTRTL; else ex &= ~WS_EX_LAYOUTRTL;
    SetWindowLongPtrW(st->hwnd, GWL_EXSTYLE, ex);

    SetWindowTextW(st->hwnd, fa ? L"نصب TypeCount " TC_VERSION_WSTR : L"TypeCount " TC_VERSION_WSTR L" Setup");
    SetWindowTextW(st->btn_lang, fa ? L"English" : L"فارسی");

    bool s0 = (st->step == 0), s1 = (st->step == 1), s2 = (st->step == 2);
    ShowWindow(st->lbl_detected, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->lbl_dir, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->edit_dir, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->btn_browse, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->chk_startmenu, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->chk_desktop, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->chk_autostart, s0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->btn_lang, s0 ? SW_SHOW : SW_HIDE);

    ShowWindow(st->progress, s1 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->lbl_status, s1 ? SW_SHOW : SW_HIDE);

    ShowWindow(st->lbl_finish_msg, s2 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->chk_launch, (s2 && st->install_ok) ? SW_SHOW : SW_HIDE);

    if (s0) {
        bool upg = st->existing.found;
        SetWindowTextW(st->lbl_title, upg
            ? (fa ? L"بروزرسانی و نصب TypeCount " TC_VERSION_WSTR : L"Upgrade TypeCount to v" TC_VERSION_WSTR)
            : (fa ? L"نصب‌کننده TypeCount " TC_VERSION_WSTR : L"Welcome to TypeCount " TC_VERSION_WSTR L" Setup"));
        SetWindowTextW(st->lbl_sub, upg
            ? (fa ? L"نسخه قبلی شناسایی شد و بدون حذف سوابق جایگزین می‌شود."
                  : L"An existing installation was detected and will be upgraded in-place.")
            : (fa ? L"آمارگیر آفلاین و امن صفحه‌کلید و ماوس برای ویندوز"
                  : L"Offline keyboard & mouse analytics for Windows"));

        std::wstring det;
        if (st->existing.found) {
            std::wstring ver = st->existing.version.empty() ? L"1.0" : st->existing.version;
            det = fa
                ? (L"✓ نسخه قبلی شناسایی شد: v" + ver + L" — ارتقا به v" TC_VERSION_WSTR L" در همان مسیر")
                : (L"✓ Existing installation detected: v" + ver + L" — Ready to upgrade to v" TC_VERSION_WSTR);
        } else {
            det = fa
                ? L"آماده نصب نسخه " TC_VERSION_WSTR L" و ثبت در فهرست برنامه‌های ویندوز."
                : L"Ready to install v" TC_VERSION_WSTR L" and register with Windows Apps & Features.";
        }
        SetWindowTextW(st->lbl_detected, det.c_str());
        SetWindowTextW(st->lbl_dir, fa ? L"مسیر نصب:" : L"Destination folder:");
        SetWindowTextW(st->btn_browse, fa ? L"انتخاب…" : L"Browse…");
        SetWindowTextW(st->chk_startmenu, fa ? L"ایجاد میان‌بر در منوی استارت" : L"Create Start Menu shortcut");
        SetWindowTextW(st->chk_desktop, fa ? L"ایجاد میان‌بر روی دسکتاپ" : L"Create Desktop shortcut");
        SetWindowTextW(st->chk_autostart, fa ? L"اجرای خودکار همراه با ویندوز" : L"Launch TypeCount automatically at Windows startup");
        SetWindowTextW(st->btn_primary, upg ? (fa ? L"بروزرسانی" : L"Upgrade >") : (fa ? L"نصب" : L"Install >"));
        SetWindowTextW(st->btn_cancel, fa ? L"انصراف" : L"Cancel");
        EnableWindow(st->btn_primary, TRUE);
        EnableWindow(st->btn_cancel, TRUE);
    } else if (s1) {
        SetWindowTextW(st->lbl_title, fa ? L"در حال نصب TypeCount…" : L"Installing TypeCount…");
        SetWindowTextW(st->lbl_sub, fa ? L"لطفاً چند لحظه صبر کنید" : L"Please wait while TypeCount is configured");
        SetWindowTextW(st->btn_primary, fa ? L"در حال نصب…" : L"Installing…");
        EnableWindow(st->btn_primary, FALSE);
        EnableWindow(st->btn_cancel, FALSE);
    } else if (s2) {
        if (st->install_ok) {
            SetWindowTextW(st->lbl_title, fa ? L"نصب با موفقیت انجام شد!" : L"TypeCount Installed Successfully!");
            SetWindowTextW(st->lbl_sub, fa ? L"نسخه " TC_VERSION_WSTR L" آماده استفاده است" : L"Version " TC_VERSION_WSTR L" is ready to use");
            std::wstring msg = fa
                ? (L"TypeCount v" TC_VERSION_WSTR L" با موفقیت در سیستم شما نصب و در فهرست برنامه‌های ویندوز ثبت شد.\n\nمسیر نصب: " + st->target_dir)
                : (L"TypeCount v" TC_VERSION_WSTR L" has been installed successfully and registered in Windows Installed Programs.\n\nLocation: " + st->target_dir);
            SetWindowTextW(st->lbl_finish_msg, msg.c_str());
        } else {
            SetWindowTextW(st->lbl_title, fa ? L"خطا در نصب" : L"Installation Incomplete");
            SetWindowTextW(st->lbl_sub, fa ? L"عملیات کامل نشد" : L"Setup encountered an issue");
            SetWindowTextW(st->lbl_finish_msg, st->error_msg.c_str());
        }
        SetWindowTextW(st->chk_launch, fa ? L"اجرای TypeCount بلافاصله پس از پایان" : L"Launch TypeCount now");
        SetWindowTextW(st->btn_primary, fa ? L"پایان" : L"Finish");
        EnableWindow(st->btn_primary, TRUE);
        ShowWindow(st->btn_cancel, SW_HIDE);
    }
    InvalidateRect(st->hwnd, nullptr, TRUE);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WizardState* st = reinterpret_cast<WizardState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = reinterpret_cast<WizardState*>(cs->lpCreateParams);
            st->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            return TRUE;
        }
        case WM_CREATE: {
            st->brush_white = CreateSolidBrush(RGB(255, 255, 255));
            st->brush_bg = CreateSolidBrush(RGB(244, 247, 252));
            st->font_title = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            st->font_sub = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            st->font_ui = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

            auto mk = [&](const wchar_t* cls, DWORD style, DWORD ex, int x, int y, int w, int h, int id, HFONT font = nullptr) {
                HWND c = CreateWindowExW(ex, cls, L"", WS_CHILD | style, x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), st->hinst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : st->font_ui), TRUE);
                return c;
            };

            st->lbl_title = mk(L"STATIC", SS_LEFT, 0, 24, 18, 390, 28, 0, st->font_title);
            st->lbl_sub = mk(L"STATIC", SS_LEFT, 0, 24, 48, 420, 20, 0, st->font_sub);
            st->btn_lang = mk(L"BUTTON", BS_PUSHBUTTON, 0, 444, 20, 74, 28, ID_BTN_LANG);

            st->lbl_detected = mk(L"STATIC", SS_LEFT, 0, 24, 96, 492, 24, 0);
            st->lbl_dir = mk(L"STATIC", SS_LEFT, 0, 24, 134, 492, 20, 0);
            st->edit_dir = mk(L"EDIT", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 24, 158, 396, 28, ID_EDIT_DIR);
            SetWindowTextW(st->edit_dir, st->target_dir.c_str());
            st->btn_browse = mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 0, 428, 158, 88, 28, ID_BTN_BROWSE);

            st->chk_startmenu = mk(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 204, 480, 24, ID_CHK_STARTMENU);
            st->chk_desktop = mk(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 232, 480, 24, ID_CHK_DESKTOP);
            st->chk_autostart = mk(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 260, 480, 24, ID_CHK_AUTOSTART);
            SendMessageW(st->chk_startmenu, BM_SETCHECK, st->opt_startmenu ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(st->chk_desktop, BM_SETCHECK, st->opt_desktop ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(st->chk_autostart, BM_SETCHECK, st->opt_autostart ? BST_CHECKED : BST_UNCHECKED, 0);

            st->progress = mk(PROGRESS_CLASSW, PBS_SMOOTH, 0, 24, 150, 492, 24, ID_PROGRESS);
            SendMessageW(st->progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            st->lbl_status = mk(L"STATIC", SS_LEFT, 0, 24, 186, 492, 24, ID_LBL_STATUS);

            st->lbl_finish_msg = mk(L"STATIC", SS_LEFT, 0, 24, 104, 492, 110, 0);
            st->chk_launch = mk(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 230, 480, 24, ID_CHK_LAUNCH);
            SendMessageW(st->chk_launch, BM_SETCHECK, st->opt_launch ? BST_CHECKED : BST_UNCHECKED, 0);

            st->btn_primary = mk(L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 314, 326, 100, 32, ID_BTN_PRIMARY);
            st->btn_cancel = mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 0, 422, 326, 94, 32, ID_BTN_CANCEL);

            apply_texts_and_visibility(st);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT cr{};
            GetClientRect(hwnd, &cr);
            RECT header{0, 0, cr.right, 80};
            FillRect(hdc, &header, st->brush_white);
            RECT body{0, 80, cr.right, cr.bottom};
            FillRect(hdc, &body, st->brush_bg);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(214, 223, 238));
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 80, nullptr); LineTo(hdc, cr.right, 80);
            MoveToEx(hdc, 0, 310, nullptr); LineTo(hdc, cr.right, 310);
            SelectObject(hdc, old);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND ctrl = reinterpret_cast<HWND>(lp);
            SetBkMode(hdc, TRANSPARENT);
            if (ctrl == st->lbl_title || ctrl == st->lbl_sub) {
                SetTextColor(hdc, ctrl == st->lbl_title ? RGB(20, 46, 86) : RGB(86, 102, 128));
                return reinterpret_cast<LRESULT>(st->brush_white);
            }
            SetTextColor(hdc, RGB(26, 36, 54));
            return reinterpret_cast<LRESULT>(st->brush_bg);
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_BTN_LANG) {
                st->fa = !st->fa;
                apply_texts_and_visibility(st);
            } else if (id == ID_BTN_BROWSE) {
                BROWSEINFOW bi{};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = st->fa ? L"انتخاب پوشه نصب TypeCount" : L"Choose TypeCount installation folder";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t path[MAX_PATH]{};
                    if (SHGetPathFromIDListW(pidl, path)) {
                        std::wstring p = path;
                        if (p.rfind(L"TypeCount") == std::wstring::npos) p += L"\\TypeCount";
                        SetWindowTextW(st->edit_dir, p.c_str());
                    }
                    CoTaskMemFree(pidl);
                }
            } else if (id == ID_BTN_CANCEL) {
                if (!st->busy) DestroyWindow(hwnd);
            } else if (id == ID_BTN_PRIMARY) {
                if (st->step == 0 && !st->busy) {
                    wchar_t buf[1024]{};
                    GetWindowTextW(st->edit_dir, buf, 1024);
                    st->target_dir = buf[0] ? std::wstring(buf) : default_install_dir();
                    st->opt_startmenu = (SendMessageW(st->chk_startmenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    st->opt_desktop = (SendMessageW(st->chk_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    st->opt_autostart = (SendMessageW(st->chk_autostart, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    st->step = 1;
                    st->busy = true;
                    apply_texts_and_visibility(st);
                    std::thread([st]() {
                        bool ok = perform_install(st);
                        PostMessageW(st->hwnd, WM_SETUP_DONE, ok ? 1 : 0, 0);
                    }).detach();
                } else if (st->step == 2) {
                    bool launch = st->install_ok && (SendMessageW(st->chk_launch, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (launch) {
                        std::wstring exe = st->target_dir + L"\\TypeCount.exe";
                        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, st->target_dir.c_str(), SW_SHOWNORMAL);
                    }
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }
        case WM_SETUP_STEP: {
            SendMessageW(st->progress, PBM_SETPOS, wp, 0);
            auto* txt = reinterpret_cast<std::wstring*>(lp);
            if (txt) {
                SetWindowTextW(st->lbl_status, txt->c_str());
                delete txt;
            }
            return 0;
        }
        case WM_SETUP_DONE: {
            st->busy = false;
            st->install_ok = (wp != 0);
            st->step = 2;
            apply_texts_and_visibility(st);
            return 0;
        }
        case WM_CLOSE:
            if (!st->busy) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st->font_ui) DeleteObject(st->font_ui);
            if (st->font_title) DeleteObject(st->font_title);
            if (st->font_sub) DeleteObject(st->font_sub);
            if (st->brush_white) DeleteObject(st->brush_white);
            if (st->brush_bg) DeleteObject(st->brush_bg);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR cmd_line, int) {
    std::wstring args = cmd_line ? cmd_line : L"";
    bool arg_uninstall = (args.find(L"--uninstall") != std::wstring::npos) || (args.find(L"/U") != std::wstring::npos);
    bool arg_silent = (args.find(L"--silent") != std::wstring::npos) || (args.find(L"/S") != std::wstring::npos);

    if (arg_uninstall) {
        return perform_uninstall(arg_silent);
    }

    WizardState st;
    st.hinst = hinst;
    st.fa = (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PERSIAN);
    st.existing = detect_existing_installation();
    st.target_dir = (!st.existing.install_dir.empty()) ? st.existing.install_dir : default_install_dir();

    if (arg_silent) {
        bool ok = perform_install(&st);
        if (ok) {
            std::wstring exe = st.target_dir + L"\\TypeCount.exe";
            ShellExecuteW(nullptr, L"open", exe.c_str(), L"--background", st.target_dir.c_str(), SW_HIDE);
        }
        return ok ? 0 : 1;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    st.icon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hIcon = st.icon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"TypeCountSetupWizard";
    RegisterClassExW(&wc);

    int win_w = 556, win_h = 412;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"TypeCountSetupWizard",
        L"TypeCount Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        sx, sy, win_w, win_h,
        nullptr, nullptr, hinst, &st);
    if (!hwnd) return 1;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

// CEF lifecycle follows the BSD-licensed cefsimple example; see THIRD_PARTY.md.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <charconv>
#include <thread>
#include <vector>
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_sandbox_win.h"
#include "include/cef_command_ids.h"
#include "include/cef_id_mappers.h"
#include "include/cef_devtools_message_observer.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "node.h"
#include "profile.h"

namespace {
enum class Mode { Direct, Switching, Proxy, Blocked };
std::atomic<Mode> mode{Mode::Direct};
bool ProxyEnabled() { return mode == Mode::Proxy; }
bool CanBrowse() { return mode == Mode::Direct || mode == Mode::Proxy; }
Profile profile;
std::filesystem::path app_dir, data_dir;
HWND panel = nullptr, link_edit = nullptr, home_edit = nullptr, status_label = nullptr;
HWND connect_button = nullptr, open_button = nullptr, disconnect_button = nullptr;
HWND language_edit = nullptr, timezone_edit = nullptr, width_edit = nullptr, height_edit = nullptr;
HBRUSH state_brush = nullptr;
std::thread worker;
bool busy = false, closing = false, reset_requested = false;
int proxy_port = 0;
int smoke_stage = 0, smoke_result = 1;
int smoke_first_browser = 0;
bool smoke_green = false, smoke_yellow = false;
std::string smoke_scenario = "proxy";
std::string smoke_link, smoke_url;
std::filesystem::path smoke_report;
CefRefPtr<CefRequestContext> context;
std::vector<CefRefPtr<CefBrowser>> browsers;
std::vector<CefRefPtr<CefRequestContext>> request_contexts;
std::vector<std::string> languages{"zh-CN,zh,en-US,en", "en-US,en", "en-GB,en", "ja-JP,ja,en-US,en", "de-DE,de,en-US,en", "fr-FR,fr,en-US,en"};
std::vector<std::string> timezones{"Asia/Shanghai", "Asia/Tokyo", "Asia/Singapore", "America/Los_Angeles", "America/New_York", "Europe/London", "Europe/Berlin", "Europe/Paris", "UTC"};
constexpr UINT kComplete = WM_APP + 1;

std::wstring Wide(const std::string& value) { return CefString(value).ToWString(); }
std::string Utf8(const std::wstring& value) { return CefString(value).ToString(); }
std::wstring Text(HWND control) {
  std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
  text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
  return text;
}
void Status(const std::wstring& text) { SetWindowTextW(status_label, text.c_str()); }
void Buttons() {
  EnableWindow(connect_button, !busy && !closing && !ProxyEnabled());
  EnableWindow(link_edit, !busy && !ProxyEnabled() && !closing);
  EnableWindow(open_button, !busy && CanBrowse() && !closing);
  EnableWindow(disconnect_button, !busy && mode != Mode::Direct && !closing);
}
cef_color_t ModeColor() {
  if (mode == Mode::Proxy) return CefColorSetARGB(255, 46, 125, 50);
  if (mode == Mode::Blocked) return CefColorSetARGB(255, 179, 38, 30);
  return CefColorSetARGB(255, 230, 177, 32);
}
std::wstring ModeText() {
  if (mode == Mode::Proxy) return L"VLESS 已启用";
  if (mode == Mode::Switching) return L"正在切换网络";
  if (mode == Mode::Blocked) return L"代理异常 · 已阻断联网";
  return L"直连 · VLESS 未启用";
}
void UpdateAppearance() {
  for (const auto& target : request_contexts) target->SetChromeColorScheme(CEF_COLOR_VARIANT_TONAL_SPOT, ModeColor());
  if (state_brush) DeleteObject(state_brush);
  auto color = mode == Mode::Proxy ? RGB(220, 241, 222) : mode == Mode::Blocked ? RGB(251, 221, 218) : RGB(255, 241, 184);
  state_brush = CreateSolidBrush(color);
  if (panel) { SetWindowTextW(panel, (L"Tunnel Browser · " + ModeText()).c_str()); InvalidateRect(status_label, nullptr, TRUE); }
  for (const auto& browser : browsers) {
    auto hwnd = GetAncestor(browser->GetHost()->GetWindowHandle(), GA_ROOT);
    SetWindowTextW(hwnd, (L"Tunnel Browser · " + ModeText()).c_str());
  }
  Buttons();
}

class Core {
 public:
  bool Load() {
    if (invoke_ && free_) return true;
    // Go c-shared modules remain loaded until process exit, including on failure.
    if (!module_) module_ = LoadLibraryExW((app_dir / L"libXray.dll").c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module_) return false;
    invoke_ = reinterpret_cast<Invoke>(GetProcAddress(module_, "CGoInvoke"));
    free_ = reinterpret_cast<Free>(GetProcAddress(module_, "CGoFree"));
    return invoke_ && free_;
  }
  CefRefPtr<CefDictionaryValue> Call(const std::string& method, const std::string& config = "") {
    if (!Load()) return nullptr;
    std::string request = "{\"apiVersion\":3,\"method\":" + JsonString(method);
    if (!config.empty()) request += ",\"payload\":{\"xrayJson\":" + JsonString(config) + "}";
    request += "}";
    auto* result = invoke_(request.c_str());
    if (!result) return nullptr;
    auto value = CefParseJSON(CefString(result), JSON_PARSER_RFC);
    free_(result);
    return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  }
  bool Stop() { if (!module_) return true; auto r = Call("stopXray"); return r && r->GetBool("success"); }
  bool Start(const std::string& config) { auto r = Call("runXray", config); return r && r->GetBool("success"); }
 private:
  using Invoke = char* (__cdecl*)(const char*);
  using Free = void (__cdecl*)(char*);
  HMODULE module_ = nullptr;
  Invoke invoke_ = nullptr;
  Free free_ = nullptr;
} core;

bool Protect(const std::string& plain, std::string& encrypted, bool decrypt) {
  DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
  DATA_BLOB out{};
  BOOL ok = decrypt ? CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                    : CryptProtectData(&in, L"TunnelBrowser node", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
  if (!ok) return false;
  encrypted.assign(reinterpret_cast<char*>(out.pbData), out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return true;
}
bool SaveLink(const std::string& link) {
  std::string encrypted;
  if (!Protect(link, encrypted, false)) return false;
  auto temp = data_dir / L"node.dat.tmp";
  std::ofstream file(temp, std::ios::binary | std::ios::trunc);
  file.write(encrypted.data(), static_cast<std::streamsize>(encrypted.size()));
  file.close();
  return file.good() && MoveFileExW(temp.c_str(), (data_dir / L"node.dat").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
std::string LoadLink() {
  std::error_code error;
  auto size = std::filesystem::file_size(data_dir / L"node.dat", error);
  if (error || size > 32768) return "";
  std::ifstream file(data_dir / L"node.dat", std::ios::binary);
  std::string encrypted((std::istreambuf_iterator<char>(file)), {}), plain;
  if (encrypted.size() > 32768 || !Protect(encrypted, plain, true)) return "";
  return plain;
}
int FreePort() {
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == INVALID_SOCKET) return 0;
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int size = sizeof(address);
  int port = 0;
  if (bind(socket, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
      getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0) port = ntohs(address.sin_port);
  closesocket(socket);
  return port;
}
std::wstring policy_error;
bool SetPref(CefRefPtr<CefPreferenceManager> target, const std::string& name, CefRefPtr<CefValue> value) {
  CefString error;
  if (target->SetPreference(name, value, error)) return true;
  policy_error = L"无法设置必需的网络策略：" + Wide(name);
  return false;
}
bool ProxyPolicy(CefRefPtr<CefRequestContext> target, bool proxy) {
  auto settings = CefDictionaryValue::Create();
  settings->SetString("mode", proxy ? "fixed_servers" : "direct");
  if (proxy) {
    settings->SetString("server", "socks5://127.0.0.1:" + std::to_string(proxy_port));
    settings->SetString("bypass_list", "<-loopback>");
  }
  auto value = CefValue::Create(); value->SetDictionary(settings);
  if (!SetPref(target, "proxy", value)) return false;
  auto rtc = CefValue::Create(); rtc->SetString("disable_non_proxied_udp");
  if (!SetPref(target, "webrtc.ip_handling_policy", rtc)) return false;
  auto predict = CefValue::Create(); predict->SetInt(2);
  if (!SetPref(target, "net.network_prediction_options", predict)) return false;
  auto lang = CefValue::Create(); lang->SetString(profile.languages);
  if (!SetPref(target, "intl.accept_languages", lang)) return false;
  auto doh = CefValue::Create(); doh->SetString("off");
  return SetPref(CefPreferenceManager::GetGlobalPreferenceManager(), "dns_over_https.mode", doh);
}

void CreatePanel();
class Traffic : public CefRequestContextHandler, public CefResourceRequestHandler {
 public:
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> target) override {
    request_contexts.push_back(target);
    if (!ProxyPolicy(target, mode != Mode::Direct)) {
      mode = Mode::Blocked;
      if (!smoke_report.empty()) { std::ofstream report(smoke_report); report << "{\"success\":false,\"policy_error\":" << JsonString(Utf8(policy_error)) << "}"; }
      if (smoke_report.empty()) MessageBoxW(nullptr, policy_error.c_str(), L"Tunnel Browser", MB_ICONERROR);
      if (panel) PostMessageW(panel, WM_CLOSE, 0, 0);
      else CefQuitMessageLoop();
      return;
    }
    if (!panel && !closing) CreatePanel();
    UpdateAppearance();
  }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest>, bool, bool, const CefString&, bool&) override { return this; }
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
      CefRefPtr<CefCallback>) override { return CanBrowse() ? RV_CONTINUE : RV_CANCEL; }
 private:
  IMPLEMENT_REFCOUNTING(Traffic);
};

void FinishClose();
void SmokeDone(bool success);
void OpenBrowser();
void Disconnect();
void ShowSettings() { ShowWindow(panel, SW_SHOW); ShowWindow(panel, SW_RESTORE); SetForegroundWindow(panel); }
class OfflineProbe : public CefCompletionCallback {
 public:
  explicit OfflineProbe(CefRefPtr<CefBrowser> browser) : browser_(browser) {}
  void OnComplete() override { browser_->GetMainFrame()->LoadURL(smoke_url + "?offline-check"); }
 private:
  CefRefPtr<CefBrowser> browser_;
  IMPLEMENT_REFCOUNTING(OfflineProbe);
};
class BrowserClient : public CefClient, public CefLifeSpanHandler, public CefContextMenuHandler,
                      public CefDisplayHandler, public CefLoadHandler, public CefRequestHandler,
                      public CefCommandHandler, public CefDevToolsMessageObserver {
 public:
  explicit BrowserClient(std::string first_url = "") : first_url_(std::move(first_url)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    auto url = request->GetURL().ToString();
    if (url.starts_with("chrome://settings") || url.starts_with("chrome://profile")) { ShowSettings(); return true; }
    auto& env = environments_[browser->GetIdentifier()];
    if (frame->IsMain() && !env.ready && (url.starts_with("http://") || url.starts_with("https://"))) {
      env.pending_request = request; return true;
    }
    return false;
  }
  bool OnChromeCommand(CefRefPtr<CefBrowser>, int command, cef_window_open_disposition_t) override {
    if (command == cef_id_for_command_id_name("IDC_OPTIONS")) { ShowSettings(); return true; }
    return SingleProfileCommand(command);
  }
  bool IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>, int command) override { return !SingleProfileCommand(command); }
  bool IsChromeToolbarButtonVisible(cef_chrome_toolbar_button_type_t type) override { return type != CEF_CTBT_AVATAR; }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void*, size_t) override {
    if (id < 10001 || id > 10003) return;
    auto it = environments_.find(browser->GetIdentifier());
    if (it == environments_.end()) return;
    auto& env = it->second;
    env.failed = env.failed || !success;
    if (--env.pending != 0) return;
    if (env.failed) {
      Status(L"环境参数应用失败。请检查时区等设置，保存后重新启动浏览器。"); ShowSettings();
      if (smoke_stage) SmokeDone(false);
      return;
    }
    env.ready = true;
    if (env.pending_request) {
      auto request = env.pending_request; env.pending_request = nullptr;
      browser->GetMainFrame()->LoadRequest(request);
    } else if (!env.initial_url.empty()) {
      auto url = std::move(env.initial_url); browser->GetMainFrame()->LoadURL(url);
    }
  }
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
    SetWindowTextW(GetAncestor(browser->GetHost()->GetWindowHandle(), GA_ROOT), (title.ToWString() + L" · " + ModeText()).c_str());
    if (title != "TunnelBrowser Local Test") return;
    if (smoke_stage == 1) {
      smoke_green = ProxyEnabled() && context->GetChromeColorSchemeColor() == ModeColor();
      if (!smoke_green) { SmokeDone(false); return; }
      smoke_stage = 2;
      if (!core.Stop()) { SmokeDone(false); return; }
      // Keep proxy mode selected to test proxy failure rather than the guard.
      context->CloseAllConnections(new OfflineProbe(browser));
    } else if (smoke_stage == 2) SmokeDone(false);
    else if (smoke_stage == 4) {
      smoke_yellow = mode == Mode::Direct && context->GetChromeColorSchemeColor() == ModeColor();
      smoke_first_browser = browser->GetIdentifier(); smoke_stage = 5;
      browser->GetHost()->ExecuteChromeCommand(cef_id_for_command_id_name("IDC_NEW_TAB"), CEF_WOD_NEW_FOREGROUND_TAB);
    } else if (smoke_stage == 5 && browser->GetIdentifier() != smoke_first_browser) {
      SmokeDone(smoke_yellow && browsers.size() >= 2 && browser->GetHost()->GetRequestContext()->IsSharingWith(context));
    } else if (smoke_stage == 6) {
      smoke_yellow = mode == Mode::Direct && context->GetChromeColorSchemeColor() == ModeColor();
      SmokeDone(smoke_green && smoke_yellow);
    }
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode error,
      const CefString&, const CefString&) override {
    if (frame->IsMain() && smoke_stage == 1) { std::ofstream log(smoke_report.parent_path() / L"browser-error.log"); log << static_cast<int>(error); }
    if (frame->IsMain() && smoke_stage == 2) {
      if (error != ERR_PROXY_CONNECTION_FAILED && error != ERR_SOCKS_CONNECTION_FAILED) { SmokeDone(false); return; }
      mode = Mode::Blocked; UpdateAppearance(); smoke_stage = 6; Disconnect();
    }
    else if (!smoke_stage && ProxyEnabled() && (error == ERR_PROXY_CONNECTION_FAILED || error == ERR_SOCKS_CONNECTION_FAILED)) {
      mode = Mode::Blocked; UpdateAppearance();
      Status(L"本机代理不可用，已阻断联网。请重新启用 VLESS，或主动关闭 VLESS 恢复直连。");
    }
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browsers.push_back(browser);
    auto& env = environments_[browser->GetIdentifier()];
    env.initial_url = std::move(first_url_);
    if (smoke_stage == 5 && browser->GetIdentifier() != smoke_first_browser) env.initial_url = smoke_url + "?tab2";
    env.registration = browser->GetHost()->AddDevToolsMessageObserver(this);
    auto zone = CefDictionaryValue::Create(); zone->SetString("timezoneId", profile.timezone);
    auto locale = CefDictionaryValue::Create(); auto name = profile.languages.substr(0, profile.languages.find(','));
    std::replace(name.begin(), name.end(), '-', '_'); locale->SetString("locale", name);
    auto metrics = CefDictionaryValue::Create();
    metrics->SetInt("width", 0); metrics->SetInt("height", 0); metrics->SetDouble("deviceScaleFactor", 0);
    metrics->SetBool("mobile", false); metrics->SetInt("screenWidth", profile.screen_width); metrics->SetInt("screenHeight", profile.screen_height);
    metrics->SetBool("dontSetVisibleSize", true);
    browser->GetHost()->ExecuteDevToolsMethod(10001, "Emulation.setTimezoneOverride", zone);
    browser->GetHost()->ExecuteDevToolsMethod(10002, "Emulation.setLocaleOverride", locale);
    browser->GetHost()->ExecuteDevToolsMethod(10003, "Emulation.setDeviceMetricsOverride", metrics);
    UpdateAppearance();
    if (closing) browser->GetHost()->CloseBrowser(false);
  }
  bool DoClose(CefRefPtr<CefBrowser>) override { return false; }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    std::erase_if(browsers, [&](const auto& b) { return b->GetIdentifier() == browser->GetIdentifier(); });
    environments_.erase(browser->GetIdentifier());
    if (browsers.empty()) { closing = true; if (!busy) FinishClose(); }
  }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>,
      CefRefPtr<CefMenuModel> model) override {
    model->AddSeparator(); model->AddItem(MENU_ID_USER_FIRST, "代理与环境设置");
  }
  bool OnContextMenuCommand(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>,
      int id, EventFlags) override {
    if (id != MENU_ID_USER_FIRST) return false;
    ShowSettings(); return true;
  }
 private:
  struct Environment {
    bool ready = false, failed = false;
    int pending = 3;
    std::string initial_url;
    CefRefPtr<CefRequest> pending_request;
    CefRefPtr<CefRegistration> registration;
  };
  static bool SingleProfileCommand(int id) {
    for (auto name : {"IDC_NEW_INCOGNITO_WINDOW", "IDC_ADD_NEW_PROFILE", "IDC_MANAGE_CHROME_PROFILES", "IDC_SHOW_AVATAR_MENU"})
      if (id == cef_id_for_command_id_name(name)) return true;
    return false;
  }
  std::map<int, Environment> environments_;
  std::string first_url_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};
void OpenBrowser() {
  if (!CanBrowse() || closing) return;
  auto url = Utf8(Text(home_edit));
  CefURLParts parts;
  if (!CefParseURL(url, parts) || (CefString(&parts.scheme) != "https" && CefString(&parts.scheme) != "http")) {
    Status(L"网址需要以 https:// 或 http:// 开头。"); return;
  }
  CefWindowInfo window; window.SetAsPopup(nullptr, L"Tunnel Browser");
  window.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  window.bounds = CefRect(80, 60, std::min(1280, profile.screen_width - 40), std::min(850, profile.screen_height - 80));
  CefBrowserSettings settings;
  if (!CefBrowserHost::CreateBrowser(window, new BrowserClient(url), "about:blank", settings, nullptr, context)) {
    Status(L"创建浏览器窗口失败。请退出后重试。");
  }
}
class ConnectionClosed : public CefCompletionCallback {
 public:
  void OnComplete() override {}
 private:
  IMPLEMENT_REFCOUNTING(ConnectionClosed);
};
void DrainConnections() {
  // CEF 150 dereferences this callback even though its API marks it optional.
  for (const auto& target : request_contexts) target->CloseAllConnections(new ConnectionClosed());
}
void BeginConnect() {
  if (busy || ProxyEnabled() || closing) return;
  auto link = Utf8(Text(link_edit));
  std::string error;
  auto node = ParseNode(link, error);
  if (!node) { Status(Wide(error)); return; }
  busy = true; mode = Mode::Switching; UpdateAppearance(); Status(L"正在启用 VLESS，网络请求暂时暂停……");
  for (const auto& browser : browsers) browser->StopLoad();
  DrainConnections();
  auto config = MakeConfig(*node, proxy_port);
  if (smoke_stage == 1) {
    auto value = CefParseJSON(config, JSON_PARSER_RFC);
    auto log = value->GetDictionary()->GetDictionary("log");
    log->SetString("loglevel", "debug");
    log->SetString("error", (smoke_report.parent_path() / L"client-debug.log").wstring());
    config = CefWriteJSON(value, JSON_WRITER_DEFAULT);
  }
  worker = std::thread([link, config] {
    bool success = core.Stop() && core.Start(config);
    bool saved = success && SaveLink(link);
    PostMessageW(panel, kComplete, success ? (saved ? 1 : 2) : 0, 0);
  });
}
void Disconnect() {
  if (busy || mode == Mode::Direct) return;
  mode = Mode::Switching; busy = true; UpdateAppearance();
  for (const auto& browser : browsers) browser->StopLoad();
  DrainConnections();
  Status(L"正在关闭 VLESS，随后恢复直连……");
  worker = std::thread([] { bool ok = core.Stop(); PostMessageW(panel, kComplete, ok ? 3 : 4, 0); });
}
class ModeReady : public CefCompletionCallback {
 public:
  ModeReady(Mode next, std::shared_ptr<size_t> pending, std::wstring message)
    : next_(next), pending_(pending), message_(std::move(message)) {}
  void OnComplete() override {
    if (--*pending_ != 0) return;
    mode = next_; busy = false; UpdateAppearance(); Status(message_);
    if (closing) { if (browsers.empty()) FinishClose(); return; }
    if (smoke_stage == 1 && next_ == Mode::Proxy) OpenBrowser();
    if (smoke_stage == 6 && next_ == Mode::Direct && !browsers.empty()) browsers.front()->GetMainFrame()->LoadURL(smoke_url + "?direct-after-disable");
  }
 private:
  Mode next_;
  std::shared_ptr<size_t> pending_;
  std::wstring message_;
  IMPLEMENT_REFCOUNTING(ModeReady);
};
void FinishSwitch(Mode next, const std::wstring& message) {
  for (const auto& target : request_contexts) {
    if (!ProxyPolicy(target, next == Mode::Proxy)) {
      mode = Mode::Blocked; busy = false; UpdateAppearance(); Status(policy_error); return;
    }
  }
  auto pending = std::make_shared<size_t>(request_contexts.size());
  for (const auto& target : request_contexts) target->CloseAllConnections(new ModeReady(next, pending, message));
}
void FinishClose() {
  if (!browsers.empty() || busy) return;
  mode = Mode::Blocked;
  core.Stop();
  DestroyWindow(panel); panel = nullptr;
  context = nullptr;
  CefQuitMessageLoop();
}
void SmokeDone(bool success) {
  if (smoke_stage == 3 || smoke_report.empty()) return;
  smoke_stage = 3; smoke_result = success ? 0 : 1;
  std::ofstream report(smoke_report);
  report << "{\"success\":" << (success ? "true" : "false") << ",\"scenario\":" << JsonString(smoke_scenario)
    << ",\"green\":" << (smoke_green ? "true" : "false") << ",\"yellow\":" << (smoke_yellow ? "true" : "false")
    << ",\"tabs\":" << browsers.size() << "}";
  report.close();
  closing = true; mode = Mode::Blocked;
  auto copy = browsers;
  for (auto browser : copy) browser->GetHost()->CloseBrowser(true);
  if (browsers.empty()) FinishClose();
}
LRESULT CALLBACK ControlKeys(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (message == WM_KEYDOWN && w == VK_TAB) {
    SetFocus(GetNextDlgTabItem(panel, hwnd, GetKeyState(VK_SHIFT) < 0)); return 0;
  }
  if (message == WM_KEYDOWN && w == VK_RETURN) {
    if (hwnd == link_edit) { BeginConnect(); return 0; }
    if (hwnd == home_edit) { OpenBrowser(); return 0; }
    wchar_t kind[16]{}; GetClassNameW(hwnd, kind, 16);
    if (std::wstring(kind) == L"Button") { SendMessageW(hwnd, BM_CLICK, 0, 0); return 0; }
  }
  if (message == WM_KEYDOWN && w == VK_ESCAPE) { ShowWindow(panel, SW_HIDE); return 0; }
  if (message == WM_KEYDOWN && w == 'A' && GetKeyState(VK_CONTROL) < 0 && hwnd == link_edit) { SendMessageW(hwnd, EM_SETSEL, 0, -1); return 0; }
  return DefSubclassProc(hwnd, message, w, l);
}
void SaveEnvironment() {
  Profile next = profile;
  int language = static_cast<int>(SendMessageW(language_edit, CB_GETCURSEL, 0, 0));
  int timezone = static_cast<int>(SendMessageW(timezone_edit, CB_GETCURSEL, 0, 0));
  auto width = Utf8(Text(width_edit)), height = Utf8(Text(height_edit));
  auto w = std::from_chars(width.data(), width.data() + width.size(), next.screen_width);
  auto h = std::from_chars(height.data(), height.data() + height.size(), next.screen_height);
  if (language < 0 || timezone < 0 || static_cast<size_t>(language) >= languages.size() || static_cast<size_t>(timezone) >= timezones.size() ||
      w.ec != std::errc() || h.ec != std::errc() ||
      w.ptr != width.data() + width.size() || h.ptr != height.data() + height.size()) {
    Status(L"请输入有效的屏幕宽高。范围：800–7680 × 600–4320。"); return;
  }
  next.languages = languages[language]; next.timezone = timezones[timezone];
  if (!ValidProfile(next)) { Status(L"屏幕尺寸超出范围：800–7680 × 600–4320。"); return; }
  Status(SaveProfile(data_dir, next) ? L"环境参数已保存，完全退出后再次启动生效。所有标签页共享这一个 profile。" : L"保存失败，原有环境参数已保留。请检查文件权限。");
}
void ResetCurrentProfile() {
  if (busy) { Status(L"请等待网络切换完成后再清空 profile。"); return; }
  if (MessageBoxW(panel, L"将关闭全部标签页，清除当前 profile 的登录状态、Cookie、缓存、历史记录及环境参数。已保存的 VLESS 节点会保留。\n\n确定清空并退出？", L"清空当前 profile", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
  reset_requested = true; closing = true; mode = Mode::Blocked;
  auto copy = browsers;
  for (const auto& browser : copy) browser->GetHost()->CloseBrowser(true);
  if (browsers.empty()) FinishClose();
}
LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(l) == status_label && state_brush) {
    SetTextColor(reinterpret_cast<HDC>(w), RGB(30, 35, 30)); SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
    return reinterpret_cast<LRESULT>(state_brush);
  }
  if (message == WM_COMMAND) {
    switch (LOWORD(w)) {
      case 101: BeginConnect(); break;
      case 102: OpenBrowser(); break;
      case 103: Disconnect(); break;
      case 104: SaveEnvironment(); break;
      case 105: ResetCurrentProfile(); break;
    }
    return 0;
  }
  if (message == kComplete) {
    if (worker.joinable()) worker.join();
    if (w == 1 || w == 2) {
      FinishSwitch(Mode::Proxy, w == 1 ? L"VLESS 已启用。现有连接已关闭；刷新网页即可使用代理。" : L"VLESS 已启用，但节点保存失败；退出后需重新导入。");
      return 0;
    } else if (w == 3) { FinishSwitch(Mode::Direct, L"VLESS 已关闭，当前为直连模式。刷新网页即可继续浏览。"); return 0; }
    else {
      busy = false; mode = Mode::Blocked; UpdateAppearance();
      Status(w == 4 ? L"核心停止失败，联网已阻断。请退出后重试。" : L"VLESS 启动失败，联网已阻断。请检查节点，或关闭 VLESS 恢复直连。");
    }
    Buttons();
    if (smoke_stage == 1 && w == 0) { SmokeDone(false); return 0; }
    if (closing && browsers.empty()) FinishClose();
    return 0;
  }
  if (message == WM_CLOSE) {
    if (!closing && !browsers.empty()) { ShowWindow(hwnd, SW_HIDE); return 0; }
    if (busy) { Status(L"操作进行中，请等待完成后退出。"); return 0; }
    closing = true; mode = Mode::Blocked; Buttons();
    auto copy = browsers;
    for (const auto& browser : copy) browser->GetHost()->CloseBrowser(false);
    if (browsers.empty()) FinishClose();
    return 0;
  }
  return DefWindowProcW(hwnd, message, w, l);
}
void CreatePanel() {
  WNDCLASSW wc{}; wc.lpfnWndProc = PanelProc; wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"TunnelBrowserControl"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&wc);
  panel = CreateWindowW(wc.lpszClassName, L"Tunnel Browser · 代理与环境设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
    CW_USEDEFAULT, CW_USEDEFAULT, 660, 610, nullptr, nullptr, wc.hInstance, nullptr);
  auto add = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0) {
    auto control = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text,
      WS_CHILD | WS_VISIBLE | style, x, y, width, height, panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (style & WS_TABSTOP) SetWindowSubclass(control, ControlKeys, 1, 0);
    return control;
  };
  status_label = add(L"STATIC", L"直连模式 · VLESS 未启用", 0, 18, 16, 608, 44);
  add(L"STATIC", L"VLESS 节点链接（RAW/TCP + REALITY + Vision）", 0, 18, 78, 608, 24);
  link_edit = add(L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 18, 104, 608, 78);
  SendMessageW(link_edit, EM_SETLIMITTEXT, 8192, 0);
  auto saved = LoadLink(); SetWindowTextW(link_edit, Wide(saved).c_str());
  connect_button = add(L"BUTTON", L"启用 VLESS", WS_TABSTOP | BS_DEFPUSHBUTTON, 18, 196, 190, 34, 101);
  disconnect_button = add(L"BUTTON", L"关闭 VLESS · 直连", WS_TABSTOP, 226, 196, 190, 34, 103);
  open_button = add(L"BUTTON", L"新建浏览器窗口", WS_TABSTOP, 434, 196, 192, 34, 102);
  add(L"STATIC", L"当前 profile 的环境参数", 0, 18, 252, 608, 24);
  add(L"STATIC", L"语言", 0, 18, 288, 100, 22);
  language_edit = add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 136, 280, 490, 220);
  if (std::find(languages.begin(), languages.end(), profile.languages) == languages.end()) languages.push_back(profile.languages);
  for (const auto& item : languages) SendMessageW(language_edit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(item).c_str()));
  auto language = std::find(languages.begin(), languages.end(), profile.languages);
  SendMessageW(language_edit, CB_SETCURSEL, std::distance(languages.begin(), language), 0);
  add(L"STATIC", L"时区", 0, 18, 328, 100, 22);
  timezone_edit = add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 136, 320, 490, 220);
  if (std::find(timezones.begin(), timezones.end(), profile.timezone) == timezones.end()) timezones.push_back(profile.timezone);
  for (const auto& item : timezones) SendMessageW(timezone_edit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(item).c_str()));
  auto timezone = std::find(timezones.begin(), timezones.end(), profile.timezone);
  SendMessageW(timezone_edit, CB_SETCURSEL, std::distance(timezones.begin(), timezone), 0);
  add(L"STATIC", L"报告屏幕尺寸", 0, 18, 368, 112, 22);
  width_edit = add(L"EDIT", std::to_wstring(profile.screen_width).c_str(), WS_TABSTOP | ES_NUMBER, 136, 360, 130, 28);
  add(L"STATIC", L"×", 0, 280, 368, 20, 22);
  height_edit = add(L"EDIT", std::to_wstring(profile.screen_height).c_str(), WS_TABSTOP | ES_NUMBER, 312, 360, 130, 28);
  add(L"BUTTON", L"保存环境参数（重启生效）", WS_TABSTOP, 18, 408, 295, 34, 104);
  add(L"BUTTON", L"清空当前 profile…", WS_TABSTOP, 331, 408, 295, 34, 105);
  add(L"STATIC", L"所有标签页共享一个 profile。环境参数会持续保存，清空 profile 时重置。\n浏览器菜单“设置”或网页右键菜单可再次打开此窗口。", 0, 18, 458, 608, 46);
  add(L"STATIC", L"新窗口网址", 0, 18, 524, 110, 22);
  home_edit = add(L"EDIT", L"https://example.com", WS_TABSTOP | ES_AUTOHSCROLL, 136, 516, 490, 28);
  Buttons(); UpdateAppearance();
  if (smoke_stage) {
    SetWindowTextW(link_edit, Wide(smoke_link).c_str());
    SetWindowTextW(home_edit, Wide(smoke_url).c_str());
    if (smoke_stage == 1) PostMessageW(panel, WM_COMMAND, 101, 0);
    else OpenBrowser();
    CefPostDelayedTask(TID_UI, base::BindOnce([] { SmokeDone(false); }), 30000);
  } else OpenBrowser();
}

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefClient> GetDefaultClient() override { return new BrowserClient(); }
  CefRefPtr<CefRequestContextHandler> GetDefaultRequestContextHandler() override { return new Traffic(); }
  void OnBeforeCommandLineProcessing(const CefString& process, CefRefPtr<CefCommandLine> command) override {
    if (!process.empty()) return;
    // Direct mode needs normal DNS; SOCKS5 resolves proxy-mode target names remotely.
    if (!smoke_report.empty()) command->AppendSwitchWithValue("host-resolver-rules", "MAP * ~NOTFOUND, EXCLUDE 127.0.0.1");
    command->AppendSwitch("disable-quic");
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-sync");
    command->AppendSwitchWithValue("force-webrtc-ip-handling-policy", "disable_non_proxied_udp");
  }
  void OnContextInitialized() override {
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) = (data_dir / L"profile").wstring();
    CefString(&settings.accept_language_list) = profile.languages;
    context = CefRequestContext::CreateContext(settings, new Traffic());
  }
 private:
  IMPLEMENT_REFCOUNTING(App);
};

int SelfTest() {
  int checks = 0;
  auto check = [&](bool ok, const char* name) { ++checks; if (!ok) { std::cerr << "FAIL: " << name << std::endl; } return ok; };
  const std::string link = "vless://00112233-4455-6677-8899-aabbccddeeff@127.0.0.1:443?type=tcp&security=reality&flow=xtls-rprx-vision&sni=example.com&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=ab12&fp=chrome";
  std::string error;
  auto node = ParseNode(link, error);
  if (!check(node.has_value(), "valid node")) return 1;
  for (const auto& bad : {link + "&sid=ff", link + "&other=1", link + "&spx=%0A", std::string("vless://bad"),
      std::string("https://example.com"), link.substr(0, link.find("pbk=")) + "pbk=x"}) {
    if (!check(!ParseNode(bad, error), "reject malformed node")) return 1;
  }
  auto ipv6 = link; ipv6.replace(ipv6.find("127.0.0.1"), 9, "[::1]");
  auto parsed6 = ParseNode(ipv6, error);
  if (!check(parsed6 && parsed6->host == "::1", "IPv6 share URL")) return 1;
  std::string encrypted, decrypted;
  if (!check(Protect(link, encrypted, false) && encrypted != link && Protect(encrypted, decrypted, true) && decrypted == link, "DPAPI round trip")) return 1;
  auto version = core.Call("xrayVersion");
  if (!check(version && version->GetBool("success") && version->GetDictionary("data")->GetString("version") == "26.10.31", "pinned C ABI")) return 1;
  int port = FreePort();
  auto config = MakeConfig(*node, port);
  if (!check(port > 0 && core.Start(config), "start generated REALITY config")) return 1;
  if (!check(!core.Start(config), "single core instance")) return 1;
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  DWORD timeout = 2000; setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(static_cast<u_short>(port));
  bool connected = connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
  const char hello[] = {5, 1, 0}; char reply[2]{};
  bool socks = connected && send(socket, hello, 3, 0) == 3 && recv(socket, reply, 2, MSG_WAITALL) == 2 && reply[0] == 5 && reply[1] == 0;
  closesocket(socket);
  if (!check(socks, "SOCKS5 handshake")) return 1;
  if (!check(core.Stop() && core.Stop() && core.Start(config) && core.Stop(), "stop/restart without unloading DLL")) return 1;
  std::error_code fs_error;
  auto temp = std::filesystem::temp_directory_path(fs_error) / (L"TunnelBrowser-self-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  if (!check(!fs_error && std::filesystem::create_directory(temp, fs_error), "isolated profile test directory")) return 1;
  Profile original, loaded;
  if (!check(LoadProfile(temp, original) && LoadProfile(temp, loaded) && original.id == loaded.id && original.timezone == loaded.timezone, "profile persists across loads")) return 1;
  loaded.languages = "en-US,en"; loaded.timezone = "Europe/London"; loaded.screen_width = 1600;
  if (!check(SaveProfile(temp, loaded) && LoadProfile(temp, loaded) && loaded.id == original.id && loaded.screen_width == 1600, "environment settings persist without rotating profile")) return 1;
  auto invalid = loaded; invalid.screen_width = 0;
  if (!check(!SaveProfile(temp, invalid) && LoadProfile(temp, loaded) && loaded.screen_width == 1600, "invalid settings preserve previous profile")) return 1;
  std::filesystem::create_directory(temp / L"profile", fs_error);
  { std::ofstream cookie(temp / L"profile" / L"Cookies"); cookie << "test"; }
  { std::ofstream node_file(temp / L"node.dat"); node_file << "test encrypted node"; }
  if (!check(ResetProfileData(temp) && !std::filesystem::exists(temp / L"profile") && std::filesystem::exists(temp / L"node.dat"), "reset clears only browsing profile and preserves node")) return 1;
  if (!check(LoadProfile(temp, loaded) && loaded.id != original.id, "explicit reset creates a new profile")) return 1;
  std::filesystem::remove(temp / L"node.dat", fs_error); ResetProfileData(temp); std::filesystem::remove(temp, fs_error);
  if (!check(mode == Mode::Direct && CanBrowse() && !ProxyEnabled(), "default direct browsing")) return 1;
  std::cout << "PASS: " << checks << " checks; no remote node contacted" << std::endl;
  return 0;
}

int Run(HINSTANCE instance, void* sandbox) {
  CefMainArgs args(instance);
  int child = CefExecuteProcess(args, nullptr, sandbox);
  if (child >= 0) return child;
  wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr, module, MAX_PATH); app_dir = std::filesystem::path(module).parent_path();
  WSADATA winsock{}; if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
  auto command = CefCommandLine::CreateCommandLine(); command->InitFromString(GetCommandLineW());
  if (command->HasSwitch("self-test")) { int result = SelfTest(); WSACleanup(); return result; }
  if (!sandbox || command->HasSwitch("no-sandbox") || command->HasSwitch("single-process") ||
      command->HasSwitch("proxy-server") || command->HasSwitch("no-proxy-server") || command->HasSwitch("proxy-pac-url") ||
      command->HasSwitch("proxy-auto-detect") || command->HasSwitch("ignore-certificate-errors") ||
      command->HasSwitch("disable-web-security") || command->HasSwitch("remote-debugging-port")) {
    MessageBoxW(nullptr, L"启动参数不符合浏览器的安全策略。请直接运行 TunnelBrowser.exe。", L"Tunnel Browser", MB_ICONERROR); return 1;
  }
  if (command->HasSwitch("smoke-test")) {
    auto fixture = std::filesystem::path(command->GetSwitchValue("smoke-test").ToWString());
    std::ifstream file(fixture, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), {});
    auto json = CefParseJSON(text, JSON_PARSER_RFC);
    if (!json || json->GetType() != VTYPE_DICTIONARY) return 1;
    auto dict = json->GetDictionary();
    smoke_link = dict->GetString("link"); smoke_url = dict->GetString("url");
    if (!smoke_url.starts_with("http://127.0.0.1:")) return 1;
    smoke_report = fixture.parent_path() / L"result.json";
    data_dir = fixture.parent_path() / L"browser-data";
    smoke_scenario = dict->GetString("scenario");
    if (smoke_scenario.empty()) smoke_scenario = "proxy";
    smoke_stage = smoke_scenario == "direct" ? 4 : 1;
  } else {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return 1;
    data_dir = std::filesystem::path(local) / L"TunnelBrowser"; CoTaskMemFree(local);
  }
  std::error_code error; std::filesystem::create_directories(data_dir, error);
  if (error || !(proxy_port = FreePort())) return 1;
  if (!LoadProfile(data_dir, profile)) {
    if (smoke_report.empty()) MessageBoxW(nullptr, L"当前 profile 的环境参数无法读取，原文件已保留。请检查 fingerprint.json。", L"Tunnel Browser", MB_ICONERROR);
    return 1;
  }
  CefSettings settings;
  CefString(&settings.root_cache_path) = data_dir.wstring();
  CefString(&settings.cache_path) = (data_dir / L"global").wstring();
  settings.log_severity = LOGSEVERITY_DISABLE;
  CefString(&settings.locale) = "zh-CN";
  CefString(&settings.accept_language_list) = profile.languages;
  auto app = CefRefPtr<App>(new App());
  if (!CefInitialize(args, settings, app, sandbox)) return CefGetExitCode();
  CefRunMessageLoop();
  if (worker.joinable()) worker.join();
  context = nullptr; request_contexts.clear(); app = nullptr;
  CefShutdown();
  if (state_brush) { DeleteObject(state_brush); state_brush = nullptr; }
  if (reset_requested && !ResetProfileData(data_dir)) {
    MessageBoxW(nullptr, L"profile 未能完全清空，请确认所有浏览器进程已退出后再试。已保存节点没有删除。", L"Tunnel Browser", MB_ICONERROR);
    WSACleanup(); return 1;
  }
  WSACleanup(); return smoke_report.empty() ? 0 : smoke_result;
}
}

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR, int, void* sandbox, cef_version_info_t*) { return Run(instance, sandbox); }
CEF_BOOTSTRAP_EXPORT int RunConsoleMain(int, char*[], void* sandbox, cef_version_info_t*) { return Run(GetModuleHandleW(nullptr), sandbox); }

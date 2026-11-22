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
#include <utility>
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
#include "route.h"
#include "service_domains.h"

namespace {
std::atomic<Mode> mode{Mode::Direct};
std::atomic<unsigned> route_generation{0};
bool ProxyEnabled() { return mode == Mode::Proxy; }
bool CanBrowse() { return CanBrowseIn(mode); }
void ChangeMode(Mode next) { mode = next; ++route_generation; }
Profile profile;
std::filesystem::path app_dir, data_dir;
std::string startup_url = "about:blank";
HWND panel = nullptr, link_edit = nullptr, status_label = nullptr;
HWND connect_button = nullptr, disconnect_button = nullptr;
HWND language_edit = nullptr, timezone_edit = nullptr, width_edit = nullptr, height_edit = nullptr;
HBRUSH state_brush = nullptr;
HFONT panel_font = nullptr;
std::vector<std::pair<HWND, CefRect>> panel_controls;
std::thread worker;
bool busy = false, closing = false, reset_requested = false;
bool startup_failed = false;
SOCKET blocked_socket = INVALID_SOCKET;
int blocked_port = 0, proxy_port = 0;
std::map<int, std::string> paused_pages;
PageUnloads unloading;
std::optional<Mode> switch_target;
std::wstring switch_status;
#ifdef TB_TESTING
int smoke_stage = 0, smoke_result = 1;
int smoke_first_browser = 0;
bool smoke_green = false, smoke_yellow = false;
std::string smoke_scenario = "proxy";
std::string smoke_link, smoke_url;
std::filesystem::path smoke_report;
void SmokeStep(const char* step) {
  if (!smoke_report.empty()) std::ofstream(smoke_report.parent_path() / L"steps.txt", std::ios::app) << step << '\n';
}
#endif
CefRefPtr<CefRequestContext> context;
std::vector<CefRefPtr<CefBrowser>> browsers;
std::vector<CefRefPtr<CefRequestContext>> request_contexts;
std::vector<std::string> languages{"zh-CN,zh,en-US,en", "en-US,en", "en-GB,en", "ja-JP,ja,en-US,en", "de-DE,de,en-US,en", "fr-FR,fr,en-US,en"};
std::vector<std::string> timezones{"Asia/Shanghai", "Asia/Tokyo", "Asia/Singapore", "America/Los_Angeles", "America/New_York", "Europe/London", "Europe/Berlin", "Europe/Paris", "UTC"};
constexpr UINT kComplete = WM_APP + 1;
constexpr char kNewTabPage[] = "data:text/html,%3Ctitle%3ENew%20Tab%3C/title%3E";

std::wstring Wide(const std::string& value) { return CefString(value).ToWString(); }
std::string Utf8(const std::wstring& value) { return CefString(value).ToString(); }
std::wstring Text(HWND control) {
  std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
  text.resize(GetWindowTextW(control, text.data(), static_cast<int>(text.size())));
  return text;
}
void Status(const std::wstring& text) { SetWindowTextW(status_label, text.c_str()); }
void Buttons() {
  EnableWindow(connect_button, ConnectAllowed(mode, busy, closing));
  EnableWindow(link_edit, ConnectAllowed(mode, busy, closing));
  EnableWindow(disconnect_button, DisconnectAllowed(mode, busy, closing));
  SetWindowTextW(connect_button, ProxyEnabled() ? L"切换 VLESS 节点" : L"启用 VLESS");
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
  return Protect(link, encrypted, false) && WriteFileAtomically(data_dir / L"node.dat", encrypted);
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
int FreePort(SOCKET* reservation = nullptr) {
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == INVALID_SOCKET) return 0;
  BOOL exclusive = TRUE;
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int size = sizeof(address);
  int port = 0;
  if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0 &&
      bind(socket, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
      getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0) port = ntohs(address.sin_port);
  if (port && reservation) *reservation = socket;
  else closesocket(socket);
  return port;
}
std::wstring policy_error;
bool SetPref(CefRefPtr<CefPreferenceManager> target, const std::string& name, CefRefPtr<CefValue> value) {
  CefString error;
  if (target->SetPreference(name, value, error)) return true;
  auto current = target->GetPreference(name);
  if (current && current->IsEqual(value)) return true;
  policy_error = L"无法设置必需的网络策略：" + Wide(name);
  return false;
}
bool ProxyPolicy(CefRefPtr<CefRequestContext> target, Mode next) {
  auto settings = CefDictionaryValue::Create();
  settings->SetString("mode", next == Mode::Direct ? "direct" : "fixed_servers");
  if (next != Mode::Direct) {
    if (!blocked_port) blocked_port = FreePort(&blocked_socket);
    if (!blocked_port) { policy_error = L"无法保留本地阻断端口。"; return false; }
    settings->SetString("server", "socks5://127.0.0.1:" + std::to_string(next == Mode::Proxy ? proxy_port : blocked_port));
    settings->SetString("bypass_list", "<-loopback>");
  }
  auto value = CefValue::Create(); value->SetDictionary(settings);
  return SetPref(target, "proxy", value);
}
bool ConfigureProfile(CefRefPtr<CefRequestContext> target) {
  auto off = CefValue::Create(); off->SetBool(false);
  for (auto name : {"search.suggest_enabled", "translate.enabled", "safebrowsing.enabled",
                    "safebrowsing.enhanced", "browser.enable_spellchecking", "spellcheck.use_spelling_service"}) {
    if (!SetPref(target, name, off)) return false;
  }
  auto search = CefDictionaryValue::Create();
  search->SetString("short_name", "DuckDuckGo"); search->SetString("keyword", "duckduckgo.com");
  search->SetString("url", "https://duckduckgo.com/?q={searchTerms}");
  auto provider = CefValue::Create(); provider->SetDictionary(search);
  if (!SetPref(target, "default_search_provider_data.template_url_data", provider)) return false;
  auto blank = CefValue::Create(); blank->SetString(kNewTabPage);
  if (!SetPref(target, "newtab_page_location_override", blank)) return false;
  auto rtc = CefValue::Create(); rtc->SetString("disable_non_proxied_udp");
  if (!SetPref(target, "webrtc.ip_handling_policy", rtc)) return false;
  auto predict = CefValue::Create(); predict->SetInt(2);
  if (!SetPref(target, "net.network_prediction_options", predict)) return false;
  auto lang = CefValue::Create(); lang->SetString(profile.languages);
  if (!SetPref(target, "intl.accept_languages", lang)) return false;
  return true;
}

void CreatePanel();
void BlockTraffic(const std::wstring& reason);
void PageLoadEnded(int browser_id, const std::string& url);
void PageClosed(int browser_id);
#ifdef TB_TESTING
void SmokeDone(bool success);
#endif

struct Environment {
  int pending = 2;
  bool failed = false;
  CefRefPtr<CefRegistration> registration;
};
std::map<int, Environment> environments;

class ResourceGate : public CefResourceRequestHandler {
 public:
  explicit ResourceGate(unsigned generation) : generation_(generation) {}
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request,
      CefRefPtr<CefCallback>) override {
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) return RV_CONTINUE;
    auto scheme = CefString(&parts.scheme).ToString();
    if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss") return RV_CONTINUE;
    return CanBrowse() && generation_ == route_generation && !ServiceDomain(CefString(&parts.host).ToString())
      ? RV_CONTINUE : RV_CANCEL;
  }
  void OnResourceLoadComplete(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
      CefRefPtr<CefResponse> response, URLRequestStatus status, int64_t) override {
    if (status != UR_FAILED || !response || response->GetError() != ERR_PROXY_CONNECTION_FAILED || !ProxyEnabled()) return;
    auto generation = generation_;
    CefPostTask(TID_UI, base::BindOnce([](unsigned generation) {
      if (ProxyEnabled() && generation == route_generation)
        BlockTraffic(L"本机代理不可用，已阻断联网。请重新启用 VLESS，或主动关闭 VLESS 恢复直连。");
    }, generation));
  }
 private:
  const unsigned generation_;
  IMPLEMENT_REFCOUNTING(ResourceGate);
};

void StartupError() {
  startup_failed = true;
  closing = true;
  ChangeMode(Mode::Blocked);
#ifdef TB_TESTING
  if (!smoke_report.empty()) {
    std::ofstream report(smoke_report);
    report << "{\"success\":false,\"policy_error\":" << JsonString(Utf8(policy_error)) << "}";
  } else
#endif
    MessageBoxW(nullptr, policy_error.c_str(), L"Tunnel Browser", MB_ICONERROR);
  if (panel) PostMessageW(panel, WM_CLOSE, 0, 0);
  else CefQuitMessageLoop();
}

class Traffic : public CefRequestContextHandler {
 public:
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> target) override {
    request_contexts.push_back(target);
    if (!ProxyPolicy(target, mode) || !ConfigureProfile(target)) {
      StartupError();
      return;
    }
    if (!panel && !closing && context && target->IsSame(context)) CreatePanel();
    UpdateAppearance();
  }
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      CefRefPtr<CefRequest>, bool, bool, const CefString&, bool&) override { return new ResourceGate(route_generation); }
 private:
  IMPLEMENT_REFCOUNTING(Traffic);
};

void FinishClose();
void OpenBrowser(std::string url = "about:blank");
void Disconnect();
void ShowSettings() { ShowWindow(panel, SW_SHOW); ShowWindow(panel, SW_RESTORE); SetForegroundWindow(panel); }
#ifdef TB_TESTING
class OfflineProbe : public CefCompletionCallback {
 public:
  explicit OfflineProbe(CefRefPtr<CefBrowser> browser) : browser_(browser) {}
  void OnComplete() override {
    if (browser_->IsValid()) browser_->GetMainFrame()->ExecuteJavaScript("fetch('/?offline-check').catch(() => {})", {}, 0);
  }
 private:
  CefRefPtr<CefBrowser> browser_;
  IMPLEMENT_REFCOUNTING(OfflineProbe);
};
#endif
class BrowserClient : public CefClient, public CefLifeSpanHandler, public CefContextMenuHandler,
                      public CefLoadHandler, public CefRequestHandler,
#ifdef TB_TESTING
                      public CefDisplayHandler,
#endif
                      public CefCommandHandler, public CefDevToolsMessageObserver {
 public:
  explicit BrowserClient(std::string first_url = {}) : first_url_(std::move(first_url)) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
#ifdef TB_TESTING
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
#endif
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefCommandHandler> GetCommandHandler() override { return this; }
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool) override {
    auto url = request->GetURL().ToString();
    // Chromium drops blocked navigations silently; show why instead. ResourceGate still enforces the block.
    if (mode == Mode::Blocked && frame->IsMain() && IsWebUrl(url)) { if (!closing) ShowSettings(); return true; }
    if (url.starts_with("chrome://settings") || url.starts_with("chrome://profile")) { ShowSettings(); return true; }
    for (std::string_view internal : {"chrome://extensions", "chrome://webstore", "chrome://chrome-signin", "chrome://signin",
                                    "chrome://password-manager", "chrome://sync", "chrome://flags", "chrome://components"})
      if (url.starts_with(internal)) return true;
    return url.starts_with("chrome-extension://");
  }
  bool OnChromeCommand(CefRefPtr<CefBrowser>, int command, cef_window_open_disposition_t) override {
    if (command == cef_id_for_command_id_name("IDC_OPTIONS")) { ShowSettings(); return true; }
    if (command == cef_id_for_command_id_name("IDC_NEW_WINDOW")) { if (CanBrowse()) OpenBrowser(); else ShowSettings(); return true; }
    return RemovedCommand(command);
  }
  bool IsChromeAppMenuItemVisible(CefRefPtr<CefBrowser>, int command) override { return !RemovedCommand(command); }
  bool IsChromeToolbarButtonVisible(cef_chrome_toolbar_button_type_t type) override { return type != CEF_CTBT_AVATAR; }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int id, bool success, const void* result, size_t size) override {
    if (id != 10001 && id != 10002) return;
    auto it = environments.find(browser->GetIdentifier());
    if (it == environments.end()) return;
    auto& env = it->second;
    // Same-origin tabs can share the renderer and its timezone override.
    if (!success && id == 10001 && result &&
        std::string_view(static_cast<const char*>(result), size).find("Timezone override is already in effect") != std::string_view::npos)
      success = true;
    env.failed = env.failed || !success;
    if (--env.pending == 0) {
#ifdef TB_TESTING
      SmokeStep(env.failed ? "environment failed" : "environment ready");
      if (!env.failed && smoke_stage == 5 && browser->GetIdentifier() != smoke_first_browser)
        browser->GetMainFrame()->LoadURL(smoke_url + "?tab2");
#endif
      if (!env.failed && CanBrowse() && !first_url_.empty()) browser->GetMainFrame()->LoadURL(std::exchange(first_url_, {}));
    }
    if (env.failed) EnvironmentError(env);
  }
#ifdef TB_TESTING
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override {
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
      smoke_stage = 8;
      browser->GetMainFrame()->ExecuteJavaScript("window.open('/?popup-get', '_blank')", {}, 0);
    } else if (smoke_stage == 8 && browser->GetMainFrame()->GetURL() == smoke_url + "?popup-get") {
      smoke_stage = 9;
      browser->GetMainFrame()->ExecuteJavaScript(
        "const f = document.createElement('form'); f.method='post'; f.target='_blank'; f.action='/?popup-post';"
        "const i = document.createElement('input'); i.name='check'; i.value='original'; f.append(i); document.body.append(f); f.submit();", {}, 0);
    } else if (smoke_stage == 9 && browser->GetMainFrame()->GetURL() == smoke_url + "?popup-post") {
      SmokeDone(smoke_yellow && browsers.size() >= 4 && browser->GetHost()->GetRequestContext()->IsSharingWith(context));
    } else if (smoke_stage == 6) {
      smoke_yellow = mode == Mode::Direct && context->GetChromeColorSchemeColor() == ModeColor();
      SmokeDone(smoke_green && smoke_yellow);
    }
  }
  void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode error,
      const CefString&, const CefString&) override {
    if (frame->IsMain() && smoke_stage == 1) { std::ofstream log(smoke_report.parent_path() / L"browser-error.log"); log << static_cast<int>(error); }
  }
#endif
  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override {
    if (frame->IsMain()) PageLoadEnded(browser->GetIdentifier(), frame->GetURL().ToString());
  }
  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) override {
    if (!frame->IsMain()) return;
    // A native popup can acquire its renderer during the original navigation.
    SetTimezone(browser->GetHost(), 10003);
    SetMetrics(browser->GetHost(), 10004);
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
#ifdef TB_TESTING
    SmokeStep("tab created");
#endif
    browsers.push_back(browser);
    auto& env = environments[browser->GetIdentifier()];
    env.registration = browser->GetHost()->AddDevToolsMessageObserver(this);
    auto host = browser->GetHost();
    if (!env.registration || !SetTimezone(host, 10001) || !SetMetrics(host, 10002)) EnvironmentError(env);
    CefPostDelayedTask(TID_UI, base::BindOnce([](int id) {
      auto it = environments.find(id);
      if (it != environments.end() && it->second.pending > 0 && !it->second.failed) EnvironmentError(it->second);
    }, browser->GetIdentifier()), 5000);
    UpdateAppearance();
    if (closing) browser->GetHost()->CloseBrowser(false);
  }
  bool DoClose(CefRefPtr<CefBrowser>) override { return false; }
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    std::erase_if(browsers, [&](const auto& b) { return b->GetIdentifier() == browser->GetIdentifier(); });
    environments.erase(browser->GetIdentifier());
    paused_pages.erase(browser->GetIdentifier());
    if (browsers.empty()) { closing = true; if (!busy) FinishClose(); }
    else if (!closing) CefPostDelayedTask(TID_UI, base::BindOnce([] {
      if (closing) return;
      for (const auto& remaining : browsers) SetTimezone(remaining->GetHost(), 10003);
    }), 200);
    PageClosed(browser->GetIdentifier());
  }
  void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>,
      CefRefPtr<CefMenuModel> model) override {
    model->Remove(cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_TRANSLATE"));
    model->Remove(cef_id_for_command_id_name("IDC_CONTENT_CONTEXT_PARTIAL_TRANSLATE"));
    model->AddSeparator(); model->AddItem(MENU_ID_USER_FIRST, L"代理与环境设置 · " + ModeText());
  }
  bool OnContextMenuCommand(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>,
      int id, EventFlags) override {
    if (id != MENU_ID_USER_FIRST) return false;
    ShowSettings(); return true;
  }
 private:
  static bool SetTimezone(CefRefPtr<CefBrowserHost> host, int id) {
    auto zone = CefDictionaryValue::Create(); zone->SetString("timezoneId", profile.timezone);
    return host->ExecuteDevToolsMethod(id, "Emulation.setTimezoneOverride", zone) != 0;
  }
  static bool SetMetrics(CefRefPtr<CefBrowserHost> host, int id) {
    auto metrics = CefDictionaryValue::Create();
    metrics->SetInt("width", 0); metrics->SetInt("height", 0); metrics->SetDouble("deviceScaleFactor", 0);
    metrics->SetBool("mobile", false); metrics->SetInt("screenWidth", profile.screen_width); metrics->SetInt("screenHeight", profile.screen_height);
    metrics->SetBool("dontSetVisibleSize", true);
    return host->ExecuteDevToolsMethod(id, "Emulation.setDeviceMetricsOverride", metrics) != 0;
  }
  static void EnvironmentError(Environment& env) {
    env.failed = true;
    Status(L"标签页环境参数应用失败。请检查设置，保存后重启浏览器。"); ShowSettings();
#ifdef TB_TESTING
    SmokeStep("environment unavailable");
    if (smoke_stage) SmokeDone(false);
#endif
  }
  static bool RemovedCommand(int id) {
    for (auto name : {"IDC_NEW_INCOGNITO_WINDOW", "IDC_ADD_NEW_PROFILE", "IDC_MANAGE_CHROME_PROFILES", "IDC_SHOW_AVATAR_MENU",
                      "IDC_MANAGE_GOOGLE_ACCOUNT", "IDC_SHOW_SIGNIN", "IDC_SHOW_SYNC_SETTINGS", "IDC_TURN_ON_SYNC",
                      "IDC_SHOW_TRANSLATE", "IDC_SHOW_GOOGLE_LENS_SHORTCUT", "IDC_ROUTE_MEDIA", "IDC_SEND_TAB_TO_SELF",
                      "IDC_MANAGE_EXTENSIONS", "IDC_EXTENSIONS_SUBMENU", "IDC_EXTENSIONS_SUBMENU_MANAGE_EXTENSIONS",
                      "IDC_EXTENSIONS_SUBMENU_VISIT_CHROME_WEB_STORE", "IDC_SHOW_PASSWORD_MANAGER", "IDC_FEEDBACK"})
      if (id == cef_id_for_command_id_name(name)) return true;
    return false;
  }
  std::string first_url_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};
void OpenBrowser(std::string url) {
  if (!CanBrowse() || closing || !context) return;
  CefURLParts parts;
  if (url != "about:blank" && (!CefParseURL(url, parts) || (CefString(&parts.scheme) != "https" && CefString(&parts.scheme) != "http"))) {
    Status(L"网址需要以 https:// 或 http:// 开头。"); return;
  }
  CefWindowInfo window; window.SetAsPopup(nullptr, L"Tunnel Browser");
  window.runtime_style = CEF_RUNTIME_STYLE_CHROME;
  window.bounds = CefRect(80, 60, 1180, 800);
  CefBrowserSettings settings;
#ifdef TB_TESTING
  SmokeStep("create browser");
#endif
  if (!CefBrowserHost::CreateBrowser(window, new BrowserClient(url == "about:blank" ? "" : url), "about:blank", settings, nullptr, context)) {
    Status(L"创建浏览器窗口失败。请退出后重试。");
  }
}
class ConnectionClosed : public CefCompletionCallback {
 public:
  explicit ConnectionClosed(base::OnceClosure complete = {}) : complete_(std::move(complete)) {}
  void OnComplete() override { if (complete_) std::move(complete_).Run(); }
 private:
  base::OnceClosure complete_;
  IMPLEMENT_REFCOUNTING(ConnectionClosed);
};

bool ApplyRoute(Mode target) {
  bool applied = true;
  for (const auto& c : request_contexts) {
    if (!ProxyPolicy(c, target)) applied = false;
  }
  return applied;
}
void DrainConnections() {
  // CEF 150 expects a valid callback even though the header marks it optional.
  for (const auto& c : request_contexts) c->CloseAllConnections(new ConnectionClosed);
}

void UnloadPages() {
  unloading.Begin();
  for (const auto& browser : browsers) {
    browser->StopLoad();
    auto frame = browser->GetMainFrame();
    auto url = frame->GetURL().ToString();
    if (!IsWebUrl(url)) continue;
    int id = browser->GetIdentifier();
    paused_pages.try_emplace(id, url);
    unloading.Track(id);
    frame->LoadURL("about:blank");
  }
}

void BlockTraffic(const std::wstring& reason) {
  ChangeMode(Mode::Blocked);
  busy = false;
  switch_target.reset();
  bool blocked = ApplyRoute(Mode::Blocked);
  DrainConnections();
  UnloadPages();
  UpdateAppearance();
  Status(blocked ? reason : policy_error);
  if (closing && browsers.empty()) { FinishClose(); return; }
#ifdef TB_TESTING
  if (smoke_stage == 2 && !browsers.empty()) {
    smoke_stage = 7;
    auto probe = browsers.front();
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<CefBrowser> tab) {
      if (tab->IsValid()) tab->GetMainFrame()->LoadURL(smoke_url + "?blocked");
    }, probe), 200);
    CefPostDelayedTask(TID_UI, base::BindOnce([] {
      if (smoke_stage != 7 || mode != Mode::Blocked) return;
      smoke_stage = 6;
      Disconnect();
    }), 1000);
  } else if (smoke_stage == 1) SmokeDone(false);
#endif
}

bool PauseTraffic(const wchar_t* message) {
  busy = true;
  ChangeMode(Mode::Switching);
  UpdateAppearance();
  Status(message);
  if (!ApplyRoute(Mode::Blocked)) { BlockTraffic(policy_error); return false; }
  DrainConnections();
  UnloadPages();
  return true;
}

void BeginConnect() {
  if (!ConnectAllowed(mode, busy, closing)) return;
  auto link = Utf8(Text(link_edit));
  if (link.empty()) link = LoadLink();
  std::string error;
  auto node = ParseNode(link, error);
  if (!node) { Status(Wide(error)); return; }
  // Switching from an active node goes through the blocked route; open pages never reload directly.
  if (!PauseTraffic(ProxyEnabled() ? L"正在切换 VLESS 节点，当前网页将通过新线路重新打开……"
                                   : L"正在启用 VLESS，当前网页将通过新线路重新打开……")) return;
  worker = std::thread([link, node = std::move(*node)] {
    int port = core.Stop() ? FreePort() : 0;
    if (port && !core.Start(MakeConfig(node, port))) {
      // A failed start leaves the next attempt free to choose another port.
      port = 0;
    }
    bool saved = port && SaveLink(link);
    PostMessageW(panel, kComplete, port ? (saved ? 1 : 2) : 0, port);
  });
}
void Disconnect() {
  if (!DisconnectAllowed(mode, busy, closing)) return;
  if (!PauseTraffic(L"正在关闭 VLESS，当前网页将通过直连重新打开……")) return;
  worker = std::thread([] { bool ok = core.Stop(); PostMessageW(panel, kComplete, ok ? 3 : 4, 0); });
}
void ResumeTraffic() {
  if (!switch_target || !unloading.Idle()) return;
  auto next = *switch_target;
  switch_target.reset();
  if (closing && browsers.empty()) { busy = false; FinishClose(); return; }
  if (!ApplyRoute(next)) { BlockTraffic(policy_error); return; }
  auto pending = std::make_shared<size_t>(request_contexts.size());
  auto generation = route_generation.load();
  for (const auto& c : request_contexts) {
    c->CloseAllConnections(new ConnectionClosed(base::BindOnce([](std::shared_ptr<size_t> pending, Mode next, unsigned generation) {
      if (--*pending || generation != route_generation) return;
      ChangeMode(next);
      busy = false;
      UpdateAppearance();
      Status(switch_status);
      if (closing) { if (browsers.empty()) FinishClose(); return; }
#ifdef TB_TESTING
      if (smoke_stage == 1 && next == Mode::Proxy) { OpenBrowser(smoke_url); return; }
      if (smoke_stage == 6 && next == Mode::Direct && !browsers.empty()) {
        paused_pages.clear();
        browsers.front()->GetMainFrame()->LoadURL(smoke_url + "?direct-after-disable");
        return;
      }
#endif
      for (const auto& browser : browsers) {
        auto page = paused_pages.find(browser->GetIdentifier());
        if (page != paused_pages.end() && browser->GetMainFrame()->GetURL() == "about:blank")
          browser->GetMainFrame()->LoadURL(page->second);
      }
      paused_pages.clear();
    }, pending, next, generation)));
  }
}
void PageLoadEnded(int browser_id, const std::string& url) {
  unloading.LoadEnded(browser_id, url);
  ResumeTraffic();
}
void PageClosed(int browser_id) {
  unloading.Closed(browser_id);
  ResumeTraffic();
}
void FinishSwitch(Mode next, const std::wstring& message) {
#ifdef TB_TESTING
  SmokeStep("apply route");
#endif
  switch_target = next;
  switch_status = message;
  ResumeTraffic();
  auto generation = route_generation.load();
  CefPostDelayedTask(TID_UI, base::BindOnce([](unsigned generation) {
    if (busy && generation == route_generation)
      BlockTraffic(L"关闭旧连接超时，已阻断联网。请关闭页面后重试。");
  }, generation), 7000);
}

void FinishClose() {
  if (!browsers.empty() || busy) return;
  ChangeMode(Mode::Blocked);
  core.Stop();
  DestroyWindow(panel); panel = nullptr;
  context = nullptr;
  CefQuitMessageLoop();
}
#ifdef TB_TESTING
void SmokeDone(bool success) {
  if (smoke_stage == 3 || smoke_report.empty()) return;
  smoke_stage = 3; smoke_result = success ? 0 : 1;
  std::ofstream report(smoke_report);
  report << "{\"success\":" << (success ? "true" : "false") << ",\"scenario\":" << JsonString(smoke_scenario)
    << ",\"green\":" << (smoke_green ? "true" : "false") << ",\"yellow\":" << (smoke_yellow ? "true" : "false")
    << ",\"tabs\":" << browsers.size() << "}";
  report.close();
  closing = true; ChangeMode(Mode::Blocked);
  auto copy = browsers;
  for (auto browser : copy) browser->GetHost()->CloseBrowser(true);
  if (browsers.empty()) FinishClose();
}
#endif
LRESULT CALLBACK ControlKeys(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  if (message == WM_KEYDOWN && w == VK_TAB) {
    SetFocus(GetNextDlgTabItem(panel, hwnd, GetKeyState(VK_SHIFT) < 0)); return 0;
  }
  if (message == WM_KEYDOWN && w == VK_RETURN) {
    if (hwnd == link_edit) { BeginConnect(); return 0; }
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
  reset_requested = true; closing = true; ChangeMode(Mode::Blocked);
  auto copy = browsers;
  for (const auto& browser : copy) browser->GetHost()->CloseBrowser(true);
  if (browsers.empty()) FinishClose();
}
void LayoutPanel(UINT dpi) {
  NONCLIENTMETRICSW metrics{sizeof(metrics)};
  HFONT font = SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)
    ? CreateFontIndirectW(&metrics.lfMessageFont) : nullptr;
  auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
  for (const auto& [control, rect] : panel_controls) {
    MoveWindow(control, scale(rect.x), scale(rect.y), scale(rect.width), scale(rect.height), TRUE);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  }
  if (panel_font) DeleteObject(panel_font);
  panel_font = font;
}
LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  if (message == WM_DPICHANGED) {
    auto* bounds = reinterpret_cast<const RECT*>(l);
    SetWindowPos(hwnd, nullptr, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    LayoutPanel(LOWORD(w));
    return 0;
  }
  if (message == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(l) == status_label && state_brush) {
    SetTextColor(reinterpret_cast<HDC>(w), RGB(30, 35, 30)); SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
    return reinterpret_cast<LRESULT>(state_brush);
  }
  if (message == WM_COMMAND) {
    switch (LOWORD(w)) {
      case 101: BeginConnect(); break;
      case 103: Disconnect(); break;
      case 104: SaveEnvironment(); break;
      case 105: ResetCurrentProfile(); break;
    }
    return 0;
  }
  if (message == kComplete) {
    if (worker.joinable()) worker.join();
    if (w == 1 || w == 2) {
      proxy_port = static_cast<int>(l);
      if (w == 1) SetWindowTextW(link_edit, L"");
      FinishSwitch(Mode::Proxy, w == 1 ? L"VLESS 已启用。" : L"VLESS 已启用，但节点保存失败；退出后需重新导入。");
      return 0;
    } else if (w == 3) { FinishSwitch(Mode::Direct, L"VLESS 已关闭，当前为直连模式。"); return 0; }
    BlockTraffic(w == 4 ? L"核心停止失败，联网已阻断。请退出后重试。" : L"VLESS 启动失败，联网已阻断。请重试，或关闭 VLESS 恢复直连。");
    return 0;
  }
  if (message == WM_CLOSE) {
    if (!closing && !browsers.empty()) { ShowWindow(hwnd, SW_HIDE); return 0; }
    if (busy) { Status(L"操作进行中，请等待完成后退出。"); return 0; }
    closing = true; ChangeMode(Mode::Blocked); Buttons();
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
    CW_USEDEFAULT, CW_USEDEFAULT, 620, 460, nullptr, nullptr, wc.hInstance, nullptr);
  if (!panel) { policy_error = L"无法创建设置窗口。"; StartupError(); return; }
  auto add = [&](const wchar_t* kind, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id = 0) {
    auto control = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text,
      WS_CHILD | WS_VISIBLE | style, x, y, width, height, panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
    panel_controls.emplace_back(control, CefRect(x, y, width, height));
    if (style & WS_TABSTOP) SetWindowSubclass(control, ControlKeys, 1, 0);
    return control;
  };
  status_label = add(L"STATIC", L"直连模式 · VLESS 未启用", 0, 18, 16, 568, 44);
  add(L"STATIC", L"VLESS 节点链接（RAW/TCP + REALITY + Vision）", 0, 18, 78, 568, 24);
  link_edit = add(L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 18, 104, 568, 28);
  SendMessageW(link_edit, EM_SETLIMITTEXT, 8192, 0);
  SendMessageW(link_edit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"粘贴链接，留空使用已保存的节点"));
  connect_button = add(L"BUTTON", L"启用 VLESS", WS_TABSTOP | BS_DEFPUSHBUTTON, 18, 148, 277, 34, 101);
  disconnect_button = add(L"BUTTON", L"关闭 VLESS · 直连", WS_TABSTOP, 309, 148, 277, 34, 103);
  add(L"STATIC", L"语言", 0, 18, 205, 100, 22);
  language_edit = add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 136, 200, 450, 220);
  if (std::find(languages.begin(), languages.end(), profile.languages) == languages.end()) languages.push_back(profile.languages);
  for (const auto& item : languages) SendMessageW(language_edit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(item).c_str()));
  auto language = std::find(languages.begin(), languages.end(), profile.languages);
  SendMessageW(language_edit, CB_SETCURSEL, std::distance(languages.begin(), language), 0);
  add(L"STATIC", L"时区", 0, 18, 245, 100, 22);
  timezone_edit = add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 136, 240, 450, 220);
  if (std::find(timezones.begin(), timezones.end(), profile.timezone) == timezones.end()) timezones.push_back(profile.timezone);
  for (const auto& item : timezones) SendMessageW(timezone_edit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Wide(item).c_str()));
  auto timezone = std::find(timezones.begin(), timezones.end(), profile.timezone);
  SendMessageW(timezone_edit, CB_SETCURSEL, std::distance(timezones.begin(), timezone), 0);
  add(L"STATIC", L"报告屏幕尺寸", 0, 18, 285, 112, 22);
  width_edit = add(L"EDIT", std::to_wstring(profile.screen_width).c_str(), WS_TABSTOP | ES_NUMBER, 136, 280, 130, 28);
  add(L"STATIC", L"×", 0, 280, 285, 20, 22);
  height_edit = add(L"EDIT", std::to_wstring(profile.screen_height).c_str(), WS_TABSTOP | ES_NUMBER, 312, 280, 130, 28);
  add(L"BUTTON", L"保存环境参数（重启生效）", WS_TABSTOP, 18, 336, 277, 34, 104);
  add(L"BUTTON", L"清空当前 profile…", WS_TABSTOP, 309, 336, 277, 34, 105);
  auto dpi = GetDpiForWindow(panel);
  SetWindowPos(panel, nullptr, 0, 0, MulDiv(620, dpi, 96), MulDiv(435, dpi, 96), SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
  LayoutPanel(dpi);
  Buttons(); UpdateAppearance();
#ifdef TB_TESTING
  if (smoke_stage) {
    SetWindowTextW(link_edit, Wide(smoke_link).c_str());
    if (smoke_stage == 1) PostMessageW(panel, WM_COMMAND, 101, 0);
    else OpenBrowser(smoke_url);
    CefPostDelayedTask(TID_UI, base::BindOnce([] { SmokeDone(false); }), 30000);
    return;
  }
#endif
  OpenBrowser(startup_url);
}

std::string BrowserArgument(CefRefPtr<CefCommandLine> command) {
  CefCommandLine::ArgumentList args;
  command->GetArguments(args);
  for (const auto& arg : args) {
    auto url = arg.ToString();
    if (url.starts_with("https://") || url.starts_with("http://")) return url;
  }
  return "about:blank";
}

class App : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
  CefRefPtr<CefClient> GetDefaultClient() override { return new BrowserClient(); }
  CefRefPtr<CefRequestContextHandler> GetDefaultRequestContextHandler() override { return new Traffic(); }
  void OnBeforeCommandLineProcessing(const CefString& process, CefRefPtr<CefCommandLine> command) override {
    if (!process.empty()) return;
    if (startup_url != "about:blank") command->AppendSwitchWithValue("tb-open-url", startup_url);
    for (auto name : {"disable-background-networking", "disable-breakpad", "disable-component-update",
                      "disable-component-extensions-with-background-pages", "disable-default-apps", "disable-domain-reliability",
                      "disable-extensions", "disable-quic", "disable-search-engine-choice-screen", "disable-software-rasterizer",
                      "disable-sync", "disable-updater-scheduler", "disable-webgpu", "metrics-recording-only", "no-first-run", "no-pings"})
      command->AppendSwitch(name);
    command->AppendSwitchWithValue("disable-features", "AutofillServerCommunication,BackForwardCache,"
      "CertificateTransparencyComponentUpdater,LensOverlay,MediaRouter,NetworkTimeServiceQuerying,OptimizationHints,"
      "OptimizationGuideModelDownloading,PasswordLeakDetection,PreconnectToSearch,Translate,TranslateRankerQuery,TranslateRankerEnforcement");
    command->AppendSwitchWithValue("disable-blink-features", "PushMessaging,WebGPU");
    for (auto name : {"gaia-url", "lso-url", "google-apis-url", "google-base-url", "google-url", "oauth-account-manager-url"})
      command->AppendSwitchWithValue(name, "https://tunnelbrowser.invalid/");
    // Chromium fixes the account-consistency mode at profile initialization.
    command->AppendSwitchWithValue("allow-browser-signin", "false");
    command->AppendSwitchWithValue("force-webrtc-ip-handling-policy", "disable_non_proxied_udp");
    std::string dns;
    for (auto domain : kServiceDomains) {
      if (!dns.empty()) dns += ", ";
      dns += "MAP " + std::string(domain) + " ~NOTFOUND, MAP *." + std::string(domain) + " ~NOTFOUND";
    }
#ifdef TB_TESTING
    if (!smoke_report.empty()) {
      dns += ", MAP * ~NOTFOUND, EXCLUDE 127.0.0.1";
      command->AppendSwitch("disable-popup-blocking");
      command->AppendSwitchWithValue("log-net-log", (smoke_report.parent_path() / L"netlog.json").wstring());
    }
#endif
    command->AppendSwitchWithValue("host-resolver-rules", dns);
  }
  void OnContextInitialized() override {
    auto doh = CefValue::Create(); doh->SetString("off");
    auto global = CefRequestContext::GetGlobalContext();
    if (!SetPref(CefPreferenceManager::GetGlobalPreferenceManager(), "dns_over_https.mode", doh) ||
        !ProxyPolicy(global, Mode::Blocked) || !ConfigureProfile(global)) { StartupError(); return; }
    CefRequestContextSettings settings;
    CefString(&settings.cache_path) = (data_dir / L"profile").wstring();
    CefString(&settings.accept_language_list) = profile.languages;
    context = CefRequestContext::CreateContext(settings, new Traffic());
    if (!context) { policy_error = L"无法创建浏览器 profile。"; StartupError(); }
  }
  bool OnAlreadyRunningAppRelaunch(CefRefPtr<CefCommandLine> command, const CefString&) override {
    auto url = command->GetSwitchValue("tb-open-url").ToString();
    if (CanBrowse() && panel && !closing) OpenBrowser(url.empty() ? "about:blank" : url);
    else if (panel && !closing) ShowSettings();
    return true;
  }
 private:
  IMPLEMENT_REFCOUNTING(App);
};

#ifdef TB_TESTING
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
  auto untyped = link; untyped.erase(untyped.find("type=tcp&"), 9);
  if (!check(ParseNode(untyped, error).has_value(), "omitted type defaults to RAW/TCP")) return 1;
  if (!check(ConnectAllowed(Mode::Proxy, false, false) && !ConnectAllowed(Mode::Switching, true, false),
      "switch node without a direct hop")) return 1;
  if (!check(ParseNode(link + "&spx=%2Fdocs%3Fid%3D1", error).has_value() &&
      !ParseNode(link + "&spx=https%3A%2F%2Fexample.com", error), "spider path stays within the node origin")) return 1;
  if (!check(ServiceDomain("update.googleapis.com.") && ServiceDomain("clients2.google.com") &&
      !ServiceDomain("non-googleapis.com") && !ServiceDomain("www.google.com"), "service domain boundaries")) return 1;
  std::string encrypted, decrypted;
  if (!check(Protect(link, encrypted, false) && encrypted != link && Protect(encrypted, decrypted, true) && decrypted == link, "DPAPI round trip")) return 1;
  auto version = core.Call("xrayVersion");
  if (!check(version && version->GetBool("success") && version->GetDictionary("data")->GetString("version") == XRAY_VERSION, "pinned C ABI")) return 1;
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
  if (!check(!std::filesystem::exists(temp / L"fingerprint.json.tmp"), "atomic save leaves no temporary file")) return 1;
  auto unwritable = temp / L"unwritable";
  std::filesystem::create_directories(unwritable / L"fingerprint.json", fs_error);
  if (!check(!fs_error && !SaveProfile(unwritable, loaded) && !std::filesystem::exists(unwritable / L"fingerprint.json.tmp"),
      "failed save removes its temporary file")) return 1;
  std::filesystem::remove_all(unwritable, fs_error);
  std::filesystem::create_directory(temp / L"profile", fs_error);
  { std::ofstream cookie(temp / L"profile" / L"Cookies"); cookie << "test"; }
  std::filesystem::create_directory(temp / L"Default", fs_error);
  { std::ofstream history(temp / L"Default" / L"History"); history << "test"; }
  { std::ofstream node_file(temp / L"node.dat"); node_file << "test encrypted node"; }
  if (!check(ResetProfileData(temp) && !std::filesystem::exists(temp / L"profile") && !std::filesystem::exists(temp / L"Default") &&
      std::filesystem::exists(temp / L"node.dat"), "reset clears both CEF directories and preserves node")) return 1;
  if (!check(LoadProfile(temp, loaded) && loaded.id != original.id, "explicit reset creates a new profile")) return 1;
  std::filesystem::remove(temp / L"node.dat", fs_error); ResetProfileData(temp); std::filesystem::remove(temp, fs_error);
  if (!check(mode == Mode::Direct && CanBrowse() && !ProxyEnabled(), "default direct browsing")) return 1;
  std::cout << "PASS: " << checks << " checks; no remote node contacted" << std::endl;
  return 0;
}
#endif

int Run(HINSTANCE instance, void* sandbox) {
  CefMainArgs args(instance);
  int child = CefExecuteProcess(args, nullptr, sandbox);
  if (child >= 0) return child;
  std::wstring module(512, L'\0');
  for (;;) {
    auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!size) return 1;
    if (size < module.size()) { module.resize(size); break; }
    module.resize(module.size() * 2);
  }
  app_dir = std::filesystem::path(module).parent_path();
  WSADATA winsock{}; if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
  auto command = CefCommandLine::CreateCommandLine(); command->InitFromString(GetCommandLineW());
#ifdef TB_TESTING
  if (command->HasSwitch("self-test")) { int result = SelfTest(); WSACleanup(); return result; }
#endif
  if (!sandbox || command->HasSwitch("no-sandbox") || command->HasSwitch("single-process") ||
      command->HasSwitch("proxy-server") || command->HasSwitch("no-proxy-server") || command->HasSwitch("proxy-pac-url") ||
      command->HasSwitch("proxy-auto-detect") || command->HasSwitch("ignore-certificate-errors") ||
      command->HasSwitch("disable-web-security") || command->HasSwitch("remote-debugging-port")) {
    MessageBoxW(nullptr, L"启动参数不符合浏览器的安全策略。请直接运行 TunnelBrowser.exe。", L"Tunnel Browser", MB_ICONERROR); return 1;
  }
  startup_url = BrowserArgument(command);
#ifdef TB_TESTING
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
  } else
#endif
  {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return 1;
    data_dir = std::filesystem::path(local) / L"TunnelBrowser"; CoTaskMemFree(local);
  }
  std::error_code error; std::filesystem::create_directories(data_dir, error);
  if (error) return 1;
  if (!LoadProfile(data_dir, profile)) {
#ifdef TB_TESTING
    if (smoke_report.empty())
#endif
      MessageBoxW(nullptr, (L"当前 profile 的环境参数无法读取：\n" + (data_dir / L"fingerprint.json").wstring() +
        L"\n\n请检查该文件和目录权限。若文件已损坏，可将其改名备份后重新启动；程序会生成默认环境参数，"
        L"浏览资料与已保存节点不受影响，语言、时区和屏幕尺寸需重新设置。").c_str(), L"Tunnel Browser", MB_ICONERROR);
    return 1;
  }
  CefSettings settings;
  CefString(&settings.root_cache_path) = data_dir.wstring();
  CefString(&settings.cache_path) = data_dir.wstring();
  settings.log_severity = LOGSEVERITY_DISABLE;
#ifdef TB_TESTING
  if (!smoke_report.empty()) {
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.log_file) = (smoke_report.parent_path() / L"cef.log").wstring();
  }
#endif
  settings.command_line_args_disabled = true;
  auto locale = profile.languages.substr(0, profile.languages.find(','));
  for (auto code : {"de", "fr", "ja"}) if (locale.starts_with(std::string(code) + "-")) locale = code;
  if (!std::filesystem::exists(app_dir / L"locales" / (Wide(locale) + L".pak"))) locale = "en-US";
  CefString(&settings.locale) = locale;
  CefString(&settings.accept_language_list) = profile.languages;
  SetEnvironmentVariableW(L"GOOGLE_API_KEY", L"no");
  SetEnvironmentVariableW(L"GOOGLE_DEFAULT_CLIENT_ID", L"no");
  SetEnvironmentVariableW(L"GOOGLE_DEFAULT_CLIENT_SECRET", L"no");
  auto app = CefRefPtr<App>(new App());
  if (!CefInitialize(args, settings, app, sandbox)) return CefGetExitCode();
  CefRunMessageLoop();
  if (worker.joinable()) worker.join();
  context = nullptr; request_contexts.clear(); app = nullptr;
  CefShutdown();
  if (blocked_socket != INVALID_SOCKET) closesocket(blocked_socket);
  if (state_brush) { DeleteObject(state_brush); state_brush = nullptr; }
  if (panel_font) DeleteObject(panel_font);
  if (reset_requested && !ResetProfileData(data_dir)) {
    MessageBoxW(nullptr, L"profile 未能完全清空，请确认所有浏览器进程已退出后再试。已保存节点没有删除。", L"Tunnel Browser", MB_ICONERROR);
    WSACleanup(); return 1;
  }
  WSACleanup();
#ifdef TB_TESTING
  if (!smoke_report.empty()) return smoke_result;
#endif
  return startup_failed ? 1 : 0;
}
}

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR, int, void* sandbox, cef_version_info_t*) { return Run(instance, sandbox); }
CEF_BOOTSTRAP_EXPORT int RunConsoleMain(int, char*[], void* sandbox, cef_version_info_t*) { return Run(GetModuleHandleW(nullptr), sandbox); }

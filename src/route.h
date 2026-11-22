#pragma once
// Route-switching rules without CEF or Win32 dependencies, so they can be checked on any host.
#include <set>
#include <string_view>

enum class Mode { Direct, Switching, Proxy, Blocked };

inline bool IsWebUrl(std::string_view url) { return url.starts_with("http://") || url.starts_with("https://"); }
inline bool CanBrowseIn(Mode mode) { return mode == Mode::Direct || mode == Mode::Proxy; }
// Connecting while VLESS is active switches nodes through the blocked route, never through direct.
inline bool ConnectAllowed(Mode mode, bool busy, bool closing) { return !busy && !closing && mode != Mode::Switching; }
inline bool DisconnectAllowed(Mode mode, bool busy, bool closing) { return !busy && !closing && mode != Mode::Direct; }

// Main frames that must leave web content before the next route is applied.
class PageUnloads {
 public:
  // Each switch waits only for the pages it unloads; entries from an unfinished earlier switch are dropped.
  void Begin() { pending_.clear(); }
  void Track(int browser_id) { pending_.insert(browser_id); }
  // A finished main-frame load of about:blank or any internal page means the old web page is gone.
  void LoadEnded(int browser_id, std::string_view url) { if (!IsWebUrl(url)) pending_.erase(browser_id); }
  void Closed(int browser_id) { pending_.erase(browser_id); }
  bool Idle() const { return pending_.empty(); }
 private:
  std::set<int> pending_;
};

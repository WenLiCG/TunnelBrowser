// Portable checks for the route-switching rules in src/route.h.
#include <iostream>
#include <string>
#include "route.h"

namespace {
int failures = 0;
void Check(bool ok, const std::string& name) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
}  // namespace

int main() {
  Check(IsWebUrl("https://example.com/") && IsWebUrl("http://127.0.0.1:8080/") && !IsWebUrl("about:blank") &&
        !IsWebUrl("chrome://history/") && !IsWebUrl("data:text/html,x") && !IsWebUrl("view-source:https://a/"),
        "web URL classification");
  Check(CanBrowseIn(Mode::Direct) && CanBrowseIn(Mode::Proxy) && !CanBrowseIn(Mode::Switching) &&
        !CanBrowseIn(Mode::Blocked), "browsing is allowed only on an applied route");

  // Enabling VLESS works from direct and from the red blocked state.
  Check(ConnectAllowed(Mode::Direct, false, false) && ConnectAllowed(Mode::Blocked, false, false),
        "connect from direct or blocked");
  // Changing nodes while VLESS is active must not require a hop through direct mode.
  Check(ConnectAllowed(Mode::Proxy, false, false), "switch node while VLESS is active");
  Check(!ConnectAllowed(Mode::Switching, true, false) && !ConnectAllowed(Mode::Proxy, true, false) &&
        !ConnectAllowed(Mode::Direct, false, true), "no connect while busy or closing");
  Check(DisconnectAllowed(Mode::Proxy, false, false) && DisconnectAllowed(Mode::Blocked, false, false) &&
        !DisconnectAllowed(Mode::Direct, false, false) && !DisconnectAllowed(Mode::Proxy, true, false) &&
        !DisconnectAllowed(Mode::Blocked, false, true), "explicit direct restore only from proxy or blocked");

  {
    PageUnloads pages;
    pages.Begin();
    pages.Track(1);
    pages.Track(2);
    pages.LoadEnded(1, "https://example.com/");
    Check(!pages.Idle(), "a page that is still web content keeps the switch waiting");
    pages.LoadEnded(1, "about:blank");
    pages.Closed(2);
    Check(pages.Idle(), "unloaded and closed pages release the switch");
  }
  {
    // The user opens chrome://history in a tab while its page is being unloaded.
    PageUnloads pages;
    pages.Begin();
    pages.Track(7);
    pages.LoadEnded(7, "chrome://history/");
    Check(pages.Idle(), "a tab that left web content for an internal page releases the switch");
  }
  {
    // A tab whose unload never finished must not stall every later switch.
    PageUnloads pages;
    pages.Begin();
    pages.Track(9);
    pages.Begin();
    Check(pages.Idle(), "a new switch waits only for pages it tracks");
  }

  if (failures) { std::cerr << failures << " route check(s) failed\n"; return 1; }
  std::cout << "PASS: host route checks\n";
  return 0;
}

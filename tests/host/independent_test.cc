// Independent checks for the imported fail-closed switching candidate.
// These exercise src/node.cc and src/route.h; they do not change product behavior.
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "node.h"
#include "route.h"
#include "service_domains.h"

namespace {
int failures = 0;
void Check(bool ok, const std::string& name) {
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << name << '\n';
  }
}
const std::string kUuid = "00112233-4455-6677-8899-aabbccddeeff";
const std::string kKey = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
const std::string kLink = "vless://" + kUuid + "@127.0.0.1:443?type=tcp&security=reality&flow=xtls-rprx-vision"
                          "&sni=example.com&pbk=" + kKey + "&sid=ab12&fp=chrome";

std::string ReplaceOnce(std::string link, const std::string& from, const std::string& to) {
  auto at = link.find(from);
  if (at == std::string::npos) return link;
  return link.replace(at, from.size(), to);
}
std::string Without(std::string link, const std::string& field) {
  auto at = link.find(field);
  return at == std::string::npos ? link : link.erase(at, field.size());
}
void Reject(const std::string& link, const std::string& name) {
  std::string error;
  Check(!ParseNode(link, error), name);
  Check(error.find(kUuid) == std::string::npos && error.find(kKey) == std::string::npos,
        name + " does not echo secrets");
}
std::optional<Node> Accept(const std::string& link, const std::string& name) {
  std::string error;
  auto node = ParseNode(link, error);
  Check(node.has_value() && error.empty(), name);
  return node;
}
void CheckFailClosed(const std::string& config, const std::string& name) {
  Check(config.find("\"loglevel\":\"none\"") != std::string::npos, name + " keeps logs off");
  Check(config.find("\"listen\":\"127.0.0.1\"") != std::string::npos, name + " listens on loopback");
  Check(config.find("0.0.0.0") == std::string::npos, name + " does not listen on all interfaces");
  Check(config.find("\"auth\":\"noauth\"") != std::string::npos, name + " uses noauth");
  Check(config.find("\"udp\":false") != std::string::npos && config.find("\"udp\":true") == std::string::npos,
        name + " disables UDP");
  Check(config.find("\"protocol\":\"freedom\"") == std::string::npos, name + " has no freedom outbound");
  Check(config.find("\"protocol\":\"direct\"") == std::string::npos, name + " has no direct outbound");
  Check(config.find("domainStrategy") == std::string::npos, name + " has no domainStrategy");
  Check(config.find("\"network\":\"raw\"") != std::string::npos, name + " uses RAW");
  for (const char* network : {"ws", "grpc", "xhttp", "quic", "httpupgrade"}) {
    Check(config.find(std::string("\"network\":\"") + network + "\"") == std::string::npos,
          name + " does not select " + network);
  }
  auto outbounds = config.find("\"outbounds\":");
  Check(outbounds != std::string::npos, name + " has outbounds");
  if (outbounds != std::string::npos) {
    auto protocol = config.find("\"protocol\":\"", outbounds);
    Check(protocol != std::string::npos && config.compare(protocol, 18, "\"protocol\":\"vless\"") == 0,
          name + " makes the first outbound VLESS");
  }
  Check(config.find("\"protocol\":\"blackhole\"") != std::string::npos, name + " keeps the service blackhole");
  Check(config.find("\"domain:tunnelbrowser.invalid\"") != std::string::npos, name + " blocks the test service domain");
  Check(ServiceDomain("tunnelbrowser.invalid") && !ServiceDomain("example.com"),
        name + " service-domain helper still has boundaries");
}
}  // namespace

int main() {
  auto tcp = Accept(kLink, "explicit type=tcp");
  auto raw = Accept(ReplaceOnce(kLink, "type=tcp", "type=raw"), "explicit type=raw");
  auto omitted = Accept(Without(kLink, "type=tcp&"), "omitted type defaults to RAW/TCP");
  auto empty = Accept(ReplaceOnce(kLink, "type=tcp", "type="), "empty type= defaults to RAW/TCP");
  if (!tcp || !raw || !omitted || !empty) return 1;
  auto tcp_config = MakeConfig(*tcp, 10808);
  Check(tcp_config == MakeConfig(*raw, 10808) && tcp_config == MakeConfig(*omitted, 10808) &&
            tcp_config == MakeConfig(*empty, 10808),
        "tcp, raw, omitted and empty type emit one RAW config");
  CheckFailClosed(tcp_config, "generated config");
  CheckFailClosed(MakeConfig(*omitted, 1), "omitted-type config");

  auto ipv6 = ReplaceOnce(kLink, "127.0.0.1", "[::1]");
  auto ipv6_node = Accept(Without(ipv6, "type=tcp&"), "omitted type keeps an IPv6 literal");
  Check(ipv6_node && ipv6_node->host == "::1", "omitted-type IPv6 host");

  Check(Accept(kLink + "&encryption=none", "encryption=none").has_value(), "encryption=none stays accepted");
  Check(Accept(kLink + "&encryption=", "empty encryption").has_value(), "empty encryption stays accepted");
  Check(Accept(kLink + "&headerType=none", "headerType=none").has_value(), "headerType=none stays accepted");
  Check(Accept(kLink + "&headerType=", "empty headerType").has_value(), "empty headerType stays accepted");
  Check(Accept(Without(kLink, "type=tcp&") + "&spx=%2Fdocs", "omitted type with an in-origin spx").has_value(),
        "omitted type still accepts an in-origin spx");

  for (const char* transport : {"ws", "grpc", "xhttp", "httpupgrade", "quic", "TCP", "RAW", "tcp "}) {
    Reject(ReplaceOnce(kLink, "type=tcp", std::string("type=") + transport),
           std::string("non-RAW transport stays rejected: ") + transport);
  }
  Reject(ReplaceOnce(kLink, "type=tcp", "type=%20"), "percent-encoded blank type stays rejected");
  Reject(kLink + "&type=raw", "duplicate type stays rejected");
  Reject(Without(kLink, "type=tcp&") + "&pqv=fixture", "pqv stays rejected when type is omitted");
  Reject(kLink + "&pqv=fixture", "pqv stays rejected");
  Reject(kLink + "&host=", "empty host= stays rejected");
  Reject(kLink + "&path=", "empty path= stays rejected");
  Reject(kLink + "&host=cdn.example", "host= stays rejected");
  Reject(kLink + "&path=%2Fws", "path= stays rejected");
  Reject(kLink + "&encryption=zero", "non-none encryption stays rejected");
  Reject(kLink + "&headerType=http", "non-none headerType stays rejected");
  Reject(Without(Without(kLink, "type=tcp&"), "security=reality&"), "omitted type still requires REALITY");
  Reject(ReplaceOnce(Without(kLink, "type=tcp&"), "xtls-rprx-vision", "xtls-rprx-vision-udp443"),
         "omitted type still rejects the UDP/443 flow");
  Reject(ReplaceOnce(kLink, ":443?", ":0?"), "port 0 stays rejected");
  Reject(ReplaceOnce(kLink, ":443?", ":65536?"), "port 65536 stays rejected");
  Reject(kLink.substr(0, kLink.find("@127.0.0.1")) + "@extra@127.0.0.1" + kLink.substr(kLink.find(":443")),
         "a second @ stays rejected");
  Reject(kLink + std::string(8193, ' '), "overlong input stays rejected");
  Reject(Without(kLink, "type=tcp&") + "&spx=https%3A%2F%2Fexample.com",
         "omitted type still rejects an off-origin spx");

  Check(IsWebUrl("https://example.com/path") && IsWebUrl("http://127.0.0.1/") && !IsWebUrl("HTTP://example.com/") &&
            !IsWebUrl("about:blank") && !IsWebUrl("chrome://history/") && !IsWebUrl("chrome://settings/") &&
            !IsWebUrl("chrome://password-manager") && !IsWebUrl("data:text/html,hi") && !IsWebUrl("blob:https://a/x") &&
            !IsWebUrl("file:///tmp/x") && !IsWebUrl("view-source:https://example.com/"),
        "only lowercase http(s) URLs are web content");
  Check(CanBrowseIn(Mode::Direct) && CanBrowseIn(Mode::Proxy) && !CanBrowseIn(Mode::Switching) &&
            !CanBrowseIn(Mode::Blocked),
        "browsing stays closed while switching or blocked");
  Check(ConnectAllowed(Mode::Proxy, false, false) && ConnectAllowed(Mode::Direct, false, false) &&
            ConnectAllowed(Mode::Blocked, false, false) && !ConnectAllowed(Mode::Switching, false, false) &&
            !ConnectAllowed(Mode::Proxy, true, false) && !ConnectAllowed(Mode::Blocked, false, true),
        "node switching is allowed from proxy without requiring direct, and not while busy, closing, or Switching");
  Check(!DisconnectAllowed(Mode::Direct, false, false) && DisconnectAllowed(Mode::Proxy, false, false) &&
            DisconnectAllowed(Mode::Blocked, false, false) && !DisconnectAllowed(Mode::Proxy, true, false),
        "direct mode is restored only by an explicit disconnect");
  // The helper itself does not know that app.cc holds busy true for the whole switch.
  Check(DisconnectAllowed(Mode::Switching, false, false) && !DisconnectAllowed(Mode::Switching, true, false),
        "disconnect during Switching is blocked while busy");

  {
    PageUnloads first;
    first.Begin();
    first.Track(9);
    first.LoadEnded(9, "https://example.com/still-open");
    Check(!first.Idle(), "an unfinished web unload holds the switch");
    first.LoadEnded(9, "chrome://history/");
    Check(first.Idle(), "an internal page releases that unload");
    first.Begin();
    Check(first.Idle(), "a later switch starts clear");
    first.Track(3);
    first.LoadEnded(3, "https://example.com/next");
    Check(!first.Idle(), "the later switch waits for its own page");
    first.LoadEnded(3, "about:blank");
    Check(first.Idle(), "the later switch is not poisoned by the earlier tab");
    first.LoadEnded(3, "https://example.com/after");
    Check(first.Idle(), "a web load after release does not re-arm the switch");
  }
  {
    PageUnloads pages;
    pages.Begin();
    pages.Track(1);
    pages.Track(2);
    pages.LoadEnded(1, "chrome://settings/");
    Check(!pages.Idle(), "one internal tab does not release another web tab");
    pages.Closed(2);
    Check(pages.Idle(), "closing the remaining tab releases the switch");
  }
  {
    PageUnloads pages;
    pages.Begin();
    pages.Track(4);
    pages.LoadEnded(4, "chrome://password-manager");
    pages.LoadEnded(4, "data:text/html,x");
    Check(pages.Idle(), "password-manager and data URLs are not web unloads");
  }

  if (failures) {
    std::cerr << failures << " independent check(s) failed\n";
    return 1;
  }
  std::cout << "PASS: independent host checks\n";
  return 0;
}

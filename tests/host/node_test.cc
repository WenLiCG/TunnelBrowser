// Portable checks for share-link parsing, service-domain matching and the generated Xray config.
// Built by tools/test_node_host.py on non-Windows hosts; Windows runs the same rules in the native --self-test.
#include <iostream>
#include <string>
#include <vector>
#include "node.h"
#include "service_domains.h"

namespace {
int failures = 0;
void Check(bool ok, const std::string& name) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
const std::string kUuid = "00112233-4455-6677-8899-aabbccddeeff";
const std::string kKey = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
const std::string kLink = "vless://" + kUuid + "@127.0.0.1:443?type=tcp&security=reality&flow=xtls-rprx-vision"
                          "&sni=example.com&pbk=" + kKey + "&sid=ab12&fp=chrome";
std::string Without(std::string link, const std::string& field) {
  auto at = link.find(field);
  return at == std::string::npos ? link : link.erase(at, field.size());
}
}  // namespace

int main(int argc, char** argv) {
  std::string error;
  auto node = ParseNode(kLink, error);
  Check(node && node->host == "127.0.0.1" && node->port == 443 && node->uuid == kUuid && node->sni == "example.com" &&
        node->public_key == kKey && node->short_id == "ab12" && node->fingerprint == "chrome" && error.empty(),
        "valid RAW/TCP + REALITY + Vision link");
  if (!node) return 1;

  if (argc > 1 && std::string(argv[1]) == "--config") {
    std::cout << MakeConfig(*node, 10808);
    return 0;
  }

  for (const auto& bad : std::vector<std::string>{
           kLink + "&sid=ff", kLink + "&other=1", kLink + "&spx=%0A", "vless://bad", "https://example.com",
           kLink.substr(0, kLink.find("pbk=")) + "pbk=x", Without(kLink, "&security=reality"),
           Without(kLink, "&flow=xtls-rprx-vision"), Without(kLink, "&sni=example.com"),
           "vless://" + kUuid + "@127.0.0.1?type=tcp", kLink.substr(0, kLink.find('?'))}) {
    Check(!ParseNode(bad, error), "reject malformed node: " + bad);
    Check(error.find(kUuid) == std::string::npos && error.find(kKey) == std::string::npos,
          "errors never echo node secrets");
  }

  auto ipv6 = kLink;
  ipv6.replace(ipv6.find("127.0.0.1"), 9, "[::1]");
  auto parsed6 = ParseNode(ipv6, error);
  Check(parsed6 && parsed6->host == "::1", "IPv6 share URL");

  Check(ParseNode(kLink + "&spx=%2Fdocs%3Fid%3D1", error).has_value() &&
        !ParseNode(kLink + "&spx=https%3A%2F%2Fexample.com", error), "spider path stays within the node origin");
  Check(ParseNode(kLink + "#My%20Node", error).has_value(), "share-link remark is ignored");
  Check(ParseNode(" \r\n" + kLink + "\n", error).has_value(), "surrounding whitespace is trimmed");

  // The VLESS share-link convention makes `type` optional with RAW/TCP as the default.
  auto untyped = ParseNode(Without(kLink, "type=tcp&"), error);
  Check(untyped && untyped->host == "127.0.0.1" && error.empty(), "omitted type defaults to RAW/TCP");
  auto raw = kLink;
  raw.replace(raw.find("type=tcp"), 8, "type=raw");
  Check(ParseNode(raw, error).has_value(), "type=raw is accepted");
  for (const auto& transport : {"ws", "grpc", "xhttp", "httpupgrade"}) {
    auto other = kLink;
    other.replace(other.find("type=tcp"), 8, std::string("type=") + transport);
    Check(!ParseNode(other, error), std::string("other transports stay rejected: ") + transport);
  }
  Check(!ParseNode(Without(kLink, "&flow=xtls-rprx-vision") + "&flow=xtls-rprx-vision-udp443", error),
        "UDP/443 Vision flow stays rejected");

  Check(ServiceDomain("update.googleapis.com.") && ServiceDomain("clients2.google.com") &&
        ServiceDomain("tunnelbrowser.invalid") && !ServiceDomain("non-googleapis.com") &&
        !ServiceDomain("www.google.com") && !ServiceDomain("googleapis.com.evil.example"),
        "service domain boundaries");

  if (failures) { std::cerr << failures << " host check(s) failed\n"; return 1; }
  std::cout << "PASS: host node checks\n";
  return 0;
}

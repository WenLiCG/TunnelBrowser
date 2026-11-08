#include "node.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <string_view>
#include "include/cef_parser.h"

namespace {
bool Decode(std::string_view text, std::string& out) {
  out.clear();
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned int c = static_cast<unsigned char>(text[i]);
    if (c == '%') {
      if (i + 2 >= text.size()) return false;
      auto part = text.substr(i + 1, 2);
      auto result = std::from_chars(part.data(), part.data() + 2, c, 16);
      if (result.ec != std::errc() || result.ptr != part.data() + 2) return false;
      i += 2;
    }
    if (c < 32 || c == 127) return false;
    out.push_back(static_cast<char>(c));
  }
  return true;
}
bool Hex(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
}
bool Host(std::string_view text, bool ipv6) {
  if (text.empty() || text.size() > 253) return false;
  return std::all_of(text.begin(), text.end(), [ipv6](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '.' || c == '-' || (ipv6 && c == ':');
  });
}
}

std::string JsonString(const std::string& text) {
  auto value = CefValue::Create();
  value->SetString(text);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<Node> ParseNode(const std::string& input, std::string& error) {
  error = "节点链接无效。请导入 VLESS + RAW/TCP + REALITY + Vision 链接。";
  if (input.size() > 8192) return {};
  auto start = input.find_first_not_of(" \t\r\n");
  auto end = input.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  std::string_view link(input.data() + start, end - start + 1);
  if (!link.starts_with("vless://")) return {};
  link.remove_prefix(8);
  link = link.substr(0, link.find('#'));
  auto question = link.find('?');
  if (question == std::string_view::npos) return {};
  auto authority = link.substr(0, question);
  auto at = authority.find('@');
  if (at == std::string_view::npos || authority.find('@', at + 1) != std::string_view::npos) return {};
  Node node;
  if (!Decode(authority.substr(0, at), node.uuid) || node.uuid.size() != 36) return {};
  std::string digits;
  for (size_t i = 0; i < node.uuid.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (node.uuid[i] != '-') return {};
    } else digits.push_back(node.uuid[i]);
  }
  if (!Hex(digits)) return {};
  auto address = authority.substr(at + 1);
  bool ipv6 = address.starts_with('[');
  auto colon = ipv6 ? address.find("]:") : address.rfind(':');
  if (colon == std::string_view::npos) return {};
  node.host = std::string(address.substr(ipv6 ? 1 : 0, colon - (ipv6 ? 1 : 0)));
  if (!Host(node.host, ipv6)) return {};
  auto port = address.substr(colon + (ipv6 ? 2 : 1));
  auto parsed = std::from_chars(port.data(), port.data() + port.size(), node.port);
  if (parsed.ec != std::errc() || parsed.ptr != port.data() + port.size() || node.port < 1 || node.port > 65535) return {};
  std::map<std::string, std::string> query;
  auto tail = link.substr(question + 1);
  while (!tail.empty()) {
    auto amp = tail.find('&');
    auto field = tail.substr(0, amp);
    auto equal = field.find('=');
    std::string key, value;
    if (equal == std::string_view::npos || !Decode(field.substr(0, equal), key) ||
        !Decode(field.substr(equal + 1), value) || !query.emplace(key, value).second) return {};
    if (amp == std::string_view::npos) break;
    tail.remove_prefix(amp + 1);
  }
  for (const auto& [key, value] : query) {
    if (key != "type" && key != "security" && key != "encryption" && key != "flow" &&
        key != "sni" && key != "fp" && key != "pbk" && key != "sid" && key != "spx" && key != "headerType") {
      error = "链接包含首版不支持的参数。请使用标准 RAW/TCP + REALITY + Vision 节点。";
      return {};
    }
  }
  if ((query["type"] != "tcp" && query["type"] != "raw") || query["security"] != "reality" ||
      query["flow"] != "xtls-rprx-vision" || (!query["encryption"].empty() && query["encryption"] != "none") ||
      (!query["headerType"].empty() && query["headerType"] != "none")) return {};
  node.sni = query["sni"];
  node.public_key = query["pbk"];
  node.short_id = query["sid"];
  node.fingerprint = query["fp"].empty() ? "chrome" : query["fp"];
  node.spider = query["spx"];
  if (!Host(node.sni, false) || node.public_key.size() != 43 || node.short_id.size() > 16 ||
      node.short_id.size() % 2 != 0 || !Hex(node.short_id)) return {};
  if (!std::all_of(node.public_key.begin(), node.public_key.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    })) return {};
  if (node.fingerprint != "chrome" && node.fingerprint != "firefox" && node.fingerprint != "safari" &&
      node.fingerprint != "edge" && node.fingerprint != "ios" && node.fingerprint != "android" &&
      node.fingerprint != "random" && node.fingerprint != "randomized") return {};
  error.clear();
  return node;
}

std::string MakeConfig(const Node& n, int port) {
  return "{\"log\":{\"loglevel\":\"none\"},\"inbounds\":[{\"listen\":\"127.0.0.1\",\"port\":" +
    std::to_string(port) + ",\"protocol\":\"socks\",\"settings\":{\"auth\":\"noauth\",\"udp\":false}}],"
    "\"outbounds\":[{\"protocol\":\"vless\",\"settings\":{\"address\":" + JsonString(n.host) +
    ",\"port\":" + std::to_string(n.port) + ",\"id\":" + JsonString(n.uuid) +
    ",\"encryption\":\"none\",\"flow\":\"xtls-rprx-vision\"},\"streamSettings\":{\"network\":\"raw\","
    "\"security\":\"reality\",\"realitySettings\":{\"serverName\":" + JsonString(n.sni) +
    ",\"password\":" + JsonString(n.public_key) + ",\"shortId\":" + JsonString(n.short_id) +
    ",\"fingerprint\":" + JsonString(n.fingerprint) + ",\"spiderX\":" + JsonString(n.spider) + "}}}]}";
}

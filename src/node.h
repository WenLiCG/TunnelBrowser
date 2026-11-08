#pragma once
#include <optional>
#include <string>

struct Node {
  std::string host, uuid, sni, public_key, short_id, fingerprint, spider;
  int port = 0;
};
std::optional<Node> ParseNode(const std::string& link, std::string& error);
std::string MakeConfig(const Node& node, int local_port);
std::string JsonString(const std::string& text);

#pragma once
// Host-test shim (non-Windows only): the subset of CEF's JSON writer used by JsonString() in src/node.cc.
// The real build links CEF; this stand-in only has to emit valid JSON string literals.
#include <cstdio>
#include <memory>
#include <string>

enum cef_json_writer_options_t { JSON_WRITER_DEFAULT = 0 };

class CefString {
 public:
  explicit CefString(std::string value) : value_(std::move(value)) {}
  std::string ToString() const { return value_; }
 private:
  std::string value_;
};

class CefValue {
 public:
  static std::shared_ptr<CefValue> Create() { return std::make_shared<CefValue>(); }
  void SetString(const std::string& value) { value_ = value; }
  const std::string& GetString() const { return value_; }
 private:
  std::string value_;
};

inline CefString CefWriteJSON(const std::shared_ptr<CefValue>& value, cef_json_writer_options_t) {
  std::string out = "\"";
  for (unsigned char c : value->GetString()) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 0x20 || c == 0x7f) { char escaped[8]; std::snprintf(escaped, sizeof(escaped), "\\u%04x", c); out += escaped; }
    else out += static_cast<char>(c);
  }
  return CefString(out + "\"");
}

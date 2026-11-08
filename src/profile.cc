#include "profile.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include "include/cef_parser.h"

bool ValidProfile(const Profile& p) {
  if (p.id.empty() || p.id.size() > 64 || p.languages.empty() || p.languages.size() > 128 ||
      p.timezone.empty() || p.timezone.size() > 80 || p.screen_width < 800 || p.screen_width > 7680 ||
      p.screen_height < 600 || p.screen_height > 4320) return false;
  if (!std::all_of(p.languages.begin(), p.languages.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ',';
  }) || p.languages.front() == ',' || p.languages.back() == ',' || p.languages.find(",,") != std::string::npos) return false;
  return std::all_of(p.timezone.begin(), p.timezone.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+';
  });
}
bool SaveProfile(const std::filesystem::path& root, const Profile& p) {
  if (!ValidProfile(p)) return false;
  auto dict = CefDictionaryValue::Create();
  dict->SetInt("version", 1); dict->SetString("id", p.id);
  dict->SetString("languages", p.languages); dict->SetString("timezone", p.timezone);
  dict->SetInt("screenWidth", p.screen_width); dict->SetInt("screenHeight", p.screen_height);
  auto value = CefValue::Create(); value->SetDictionary(dict);
  auto text = CefWriteJSON(value, JSON_WRITER_PRETTY_PRINT).ToString();
  auto temporary = root / L"fingerprint.json.tmp";
  std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
  file.write(text.data(), static_cast<std::streamsize>(text.size())); file.close();
  return file.good() && MoveFileExW(temporary.c_str(), (root / L"fingerprint.json").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
bool LoadProfile(const std::filesystem::path& root, Profile& p) {
  std::error_code error;
  auto path = root / L"fingerprint.json";
  if (!std::filesystem::exists(path, error)) {
    if (error) return false;
    GUID id{}; wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, 40)) return false;
    p.id = CefString(text).ToString();
    p.screen_width = std::max(800, GetSystemMetrics(SM_CXSCREEN));
    p.screen_height = std::max(600, GetSystemMetrics(SM_CYSCREEN));
    return SaveProfile(root, p);
  }
  auto size = std::filesystem::file_size(path, error);
  if (error || size > 16384) return false;
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)), {});
  auto value = CefParseJSON(text, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) return false;
  auto dict = value->GetDictionary();
  if (dict->GetInt("version") != 1) return false;
  p.id = dict->GetString("id"); p.languages = dict->GetString("languages"); p.timezone = dict->GetString("timezone");
  p.screen_width = dict->GetInt("screenWidth"); p.screen_height = dict->GetInt("screenHeight");
  return ValidProfile(p);
}
bool ResetProfileData(const std::filesystem::path& root) {
  std::error_code error;
  auto base = std::filesystem::canonical(root, error);
  if (error || base == base.root_path()) return false;
  // Only these app-owned children may be cleared; refuse junctions escaping root.
  for (const auto* name : {L"profile", L"global", L"fingerprint.json", L"fingerprint.json.tmp", L"Local State"}) {
    auto target = std::filesystem::weakly_canonical(base / name, error);
    if (error || target.parent_path() != base) return false;
  }
  for (const auto* name : {L"profile", L"global", L"fingerprint.json", L"fingerprint.json.tmp", L"Local State"}) {
    std::filesystem::remove_all(base / name, error);
    if (error) return false;
  }
  return true;
}

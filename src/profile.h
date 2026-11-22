#pragma once
#include <filesystem>
#include <string>

struct Profile {
  std::string id;
  std::string languages = "zh-CN,zh,en-US,en";
  std::string timezone = "Asia/Shanghai";
  int screen_width = 1920, screen_height = 1080;
};
// Writes |target|.tmp, flushes it to disk and renames it over |target|; removes the temporary file on failure.
bool WriteFileAtomically(const std::filesystem::path& target, const std::string& data);
bool ValidProfile(const Profile& profile);
bool LoadProfile(const std::filesystem::path& root, Profile& profile);
bool SaveProfile(const std::filesystem::path& root, const Profile& profile);
bool ResetProfileData(const std::filesystem::path& root);

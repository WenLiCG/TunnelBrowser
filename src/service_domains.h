#pragma once
#include <string_view>

// Browser integrations and their backends. Public site navigation uses the standard URL path.
inline constexpr std::string_view kServiceDomains[] = {
  "accounts.google.com", "apis.google.com", "chrome.google.com", "chromewebstore.google.com",
  "clients1.google.com", "clients2.google.com", "clients3.google.com", "clients4.google.com",
  "clients5.google.com", "clients6.google.com", "dns.google",
  "doubleclick.net", "firebaseio.com", "google-analytics.com", "googleadservices.com", "googleapis.com",
  "googlesyndication.com", "googletagmanager.com", "gstatic.com", "gvt1.com", "gvt2.com",
  "lens.google.com", "mtalk.google.com", "passwords.google.com", "play.google.com", "recaptcha.net",
  "safebrowsing.google.com", "stun.l.google.com", "time.google.com", "translate.google.com",
  "translate.googleusercontent.com", "tunnelbrowser.invalid", "update.google.com",
};

inline bool ServiceDomain(std::string_view host) {
  if (host.ends_with('.')) host.remove_suffix(1);
  for (auto domain : kServiceDomains) {
    if (host == domain || (host.size() > domain.size() && host.ends_with(domain) &&
                          host[host.size() - domain.size() - 1] == '.')) return true;
  }
  return false;
}

#pragma once
// Host-test shim (non-Windows only): InetPtonA has the same contract as POSIX inet_pton.
#include <arpa/inet.h>

using IN6_ADDR = in6_addr;
inline int InetPtonA(int family, const char* text, void* address) { return inet_pton(family, text, address); }

# Third-party components

| Component | Version / source | License |
| --- | --- | --- |
| Chromium Embedded Framework | `150.0.23+g4ccc4ca+chromium-150.0.7871.287`; [official binaries](https://cef-builds.spotifycdn.com/index.html), [CEF source](https://github.com/chromiumembedded/cef/tree/4ccc4ca), [Chromium source](https://chromium.googlesource.com/chromium/src/+/refs/tags/150.0.7871.287/) | CEF BSD; Chromium and bundled third-party notices are available in the runtime's `chrome://credits`. CEF notice is packaged in `licenses/CEF-BSD.txt`. |
| libXray | [v26.10.31 release](https://github.com/XTLS/libXray/releases/tag/v26.10.31); [matching source](https://github.com/XTLS/libXray/tree/7fe8528063e54343af8ba9a37431afc56000de21) | MIT, packaged in `licenses/libXray-MIT.txt`. Binary is unmodified. |
| Xray-core (inside libXray) | [matching source](https://github.com/XTLS/Xray-core/tree/a4a3dff5b20c2faae567b742c172c7b95e75c083); [complete source archive](https://github.com/XTLS/Xray-core/archive/a4a3dff5b20c2faae567b742c172c7b95e75c083.zip) | MPL 2.0, packaged in `licenses/Xray-core-MPL-2.0.txt`. Covered source is available at the preceding links without charge. No local changes. See libXray's matching `go.mod`/`go.sum` for bundled Go dependencies and their licenses. |

The application lifecycle and CMake integration follow the CEF `cefsimple` example and CEF binary distribution macros. Their notice is reproduced below and in the CEF license file:

Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of Google Inc. nor the name Chromium Embedded Framework nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Local build tooling is not included in the browser package. On the development machine, MSVC/Windows SDK packages were fetched from Microsoft's official manifests using the reviewed [portable-msvc.py](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977/553d55bb7f0405a2fbd0228b8ed87c2b8ad200f8), without installing a machine-wide toolchain. Compiler packages remain subject to Microsoft's terms. CMake and Ninja are build-only tools.

The SDK archives were extracted locally with [lessmsi v2.12.9](https://github.com/activescott/lessmsi/releases/tag/v2.12.9) (MIT), after the system MSI extractor stalled. This build-only tool is also excluded from the application package.

## Fingerprint environment research (0.2)

Reviewed [Camoufox](https://github.com/daijro/camoufox), [fingerprint-suite](https://github.com/apify/fingerprint-suite) and [fingerprint-chromium](https://github.com/adryfish/fingerprint-chromium) as primary-source references for coherent, persistent environments and the distinction between environment settings and patched-engine fingerprints. No source code or binary from these projects is bundled or copied into this release.

The implementation uses CEF's `SetChromeColorScheme`, native language preferences and the official Chromium [Emulation protocol](https://chromedevtools.github.io/devtools-protocol/tot/Emulation/) for timezone, locale and screen metrics. It does not implement these projects' Canvas/WebGL/audio engine patches. CEF 150's `CloseAllConnectionsInternal` unconditionally calls its completion callback despite the public optional annotation; application callers always supply a callback. [Owning CEF source](https://github.com/chromiumembedded/cef/blob/4ccc4ca/libcef/browser/request_context_impl.cc).

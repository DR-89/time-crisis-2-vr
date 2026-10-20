# Time Crisis II VR 0.3.1 — local test build

Added launcher artwork at five Android densities, an adaptive icon and a banner.
Application, game activity and setup activity declare the artwork, including the
ROM-free launcher variant. APK packaging now compiles and links the resources.
Version code 9 retains the existing package identity and signing key.

The icon update repackages the existing native game libraries and supplied game
content. The rendering, controls and game behaviour are unchanged.

The build can use `ANDROID_SDK_ROOT`, `ANDROID_BUILD_TOOLS_VERSION` and
`ANDROID_NDK_VERSION` when the local toolchain differs from the pinned defaults.
The existing SDK junction on this machine points to a missing folder, so local
packaging used the working API 35 / build tools 35.0.0 / NDK 29 installation.

APK signature and 16 KiB ZIP alignment pass. All five legacy icons are opaque
and nonblank; adaptive foreground/background references and the banner resolve.
All 29 DEX/native/model/ROM files are byte-identical to 0.3.0.

Headset/QGO validation remains pending. This build is not published.

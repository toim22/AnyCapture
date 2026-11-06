# Third-party libraries, downloaded by CMake at configure time (pinned release archives with their SHA-256) and
# built as static libraries with the same static CRT as Snapnab. Their licenses are in THIRD-PARTY-NOTICES.txt.
#
#   libwebp 1.5.0     (BSD-3-Clause)  WebP encoder and decoder (Save as / open WebP, Copy as data URL)
#   ZXing-C++ 2.2.1   (Apache-2.0)    QR code and barcode reader
#   Corrosion 0.6.1   (MIT)           builds the Rust crates with Cargo (SNAPNAB_RUST, rust/snapcore)
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)        # archive timestamps = extraction time
endif()

# ---- libwebp: only the libraries (no tools, no mux/demux)
set(WEBP_BUILD_ANIM_UTILS OFF)
set(WEBP_BUILD_CWEBP OFF)
set(WEBP_BUILD_DWEBP OFF)
set(WEBP_BUILD_GIF2WEBP OFF)
set(WEBP_BUILD_IMG2WEBP OFF)
set(WEBP_BUILD_VWEBP OFF)
set(WEBP_BUILD_WEBPINFO OFF)
set(WEBP_BUILD_LIBWEBPMUX OFF)
set(WEBP_BUILD_WEBPMUX OFF)
set(WEBP_BUILD_EXTRAS OFF)
FetchContent_Declare(libwebp
    URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.5.0.tar.gz
    URL_HASH SHA256=668c9aba45565e24c27e17f7aaf7060a399f7f31dba6c97a044e1feacb930f37
    EXCLUDE_FROM_ALL)

# ---- ZXing-C++: readers only
set(BUILD_WRITERS OFF)
set(BUILD_READERS ON)
set(BUILD_EXAMPLES OFF)
set(BUILD_BLACKBOX_TESTS OFF)
set(BUILD_UNIT_TESTS OFF)
set(BUILD_PYTHON_MODULE OFF)
set(BUILD_C_API OFF)
set(BUILD_SHARED_LIBS OFF)
set(LINK_CPP_STATICALLY ON)
FetchContent_Declare(zxing
    URL https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v2.2.1.tar.gz
    URL_HASH SHA256=02078ae15f19f9d423a441f205b1d1bee32349ddda7467e2c84e8f08876f8635
    EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(libwebp zxing)

# ---- Rust (pilot of the Rust migration): rust/snapcore reads and writes the .snap container. Needs Cargo (rustup.rs);
#      -DSNAPNAB_RUST=OFF builds without it (the .snap container then uses the Windows structured storage API).
option(SNAPNAB_RUST "Build the Rust parts (rust/snapcore)" ON)
if(SNAPNAB_RUST)
    FetchContent_Declare(corrosion
        URL https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz
        URL_HASH SHA256=e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3)
    FetchContent_MakeAvailable(corrosion)
endif()

# Third-party libraries, downloaded by CMake at configure time (pinned release archives with their SHA-256) and
# built as static libraries with the same static CRT as Snapnab. Their licenses are in THIRD-PARTY-NOTICES.txt.
#
#   ZXing-C++ 2.2.1   (Apache-2.0)    QR code and barcode reader
#   Corrosion 0.6.1   (MIT)           builds the Rust workspace (rust/) with Cargo
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)        # archive timestamps = extraction time
endif()

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

FetchContent_MakeAvailable(zxing)

# ---- Rust: the workspace in rust/ (the C++ to Rust migration, docs/ARCHITECTURE.md). Needs Cargo (rustup.rs). Its
# crates, libwebp 1.5.0 among them (libwebp-sys), are pinned in rust/Cargo.lock.
FetchContent_Declare(corrosion
    URL https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz
    URL_HASH SHA256=e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3)
FetchContent_MakeAvailable(corrosion)

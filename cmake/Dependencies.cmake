# Third-party code, downloaded by CMake at configure time (pinned release archives with their SHA-256). The libraries
# compiled into the programs are Rust crates pinned in rust/Cargo.lock; their licenses are in THIRD-PARTY-NOTICES.txt.
#
#   Corrosion 0.6.1   (MIT)           builds the Rust workspace (rust/) with Cargo
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)        # archive timestamps = extraction time
endif()

# ---- Rust: the workspace in rust/ (the C++ to Rust migration, docs/ARCHITECTURE.md). Needs Cargo (rustup.rs). Its
# crates, libwebp 1.5.0 among them (libwebp-sys), are pinned in rust/Cargo.lock.
FetchContent_Declare(corrosion
    URL https://github.com/corrosion-rs/corrosion/archive/refs/tags/v0.6.1.tar.gz
    URL_HASH SHA256=e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3)
FetchContent_MakeAvailable(corrosion)

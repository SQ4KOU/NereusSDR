#!/usr/bin/env bash
# Keep macOS CI and release builds on OpenSSL 3 (libcrypto.3 / libssl.3).
#
# Why: every bench and release build so far ships libcrypto.3, and moving to
# OpenSSL 4 would need benching before a release is cut. Homebrew's default
# openssl formula is now OpenSSL 4 on some runner images (openssl@4, not
# keg-only, and a dependency of ccache, wget and others, so any brew install
# can link it). Linked into the Homebrew prefix, it sits where the configure
# searches (CMAKE_PREFIX_PATH="$(brew --prefix)") and where CMakeLists.txt
# points every compile (include_directories on the Homebrew prefix), so
# FindOpenSSL picked /opt/homebrew/lib/libcrypto.dylib 4.0.3 even after
# `brew install openssl@3`, and its headers would win over the keg's.
#
# So each macOS configure:
#   1. runs `unlink` after the job's last brew install, which leaves no
#      OpenSSL headers or libraries in the Homebrew prefix (the opt/ paths
#      other formulas link against stay in place);
#   2. passes -DOPENSSL_ROOT_DIR at the OpenSSL it means to use (the openssl@3
#      keg, or release.yml's own OpenSSL 3.0.21 build on the x86_64 row).
#      FindOpenSSL then searches only that root, and CMakeLists.txt fails the
#      configure if the headers it found are not under it.
# The other commands prove what was found and linked.
#
#   scripts/ci/macos-openssl.sh unlink
#   scripts/ci/macos-openssl.sh check-cache <CMakeCache.txt> <expected lib dir>
#   scripts/ci/macos-openssl.sh check-link <build dir> <expected lib dir>
#   scripts/ci/macos-openssl.sh check-bundle <app bundle>
set -euo pipefail

# Prints every Mach-O file under the given paths (files or directories).
list_machos() {
    find "$@" -type f 2>/dev/null | while IFS= read -r bin; do
        if file -b "$bin" | grep -q '^Mach-O'; then
            printf '%s\n' "$bin"
        fi
    done
}

# Prints "binary: reference" for every libcrypto or libssl a binary links.
openssl_refs() {
    list_machos "$@" | while IFS= read -r bin; do
        otool -L "$bin" | tail -n +2 | awk -v b="$bin" '{print b ": " $1}'
    done | grep -E '/lib(crypto|ssl)[.0-9]*[.]dylib$' || true
}

cmd_unlink() {
    local f p prefix
    for f in openssl openssl@3 openssl@4; do
        if brew list --formula "$f" >/dev/null 2>&1; then
            brew unlink "$f"
        fi
    done
    prefix="$(brew --prefix)"
    for p in include/openssl lib/libcrypto.dylib lib/libssl.dylib lib/pkgconfig/openssl.pc; do
        if [ -e "$prefix/$p" ] || [ -L "$prefix/$p" ]; then
            echo "::error::$prefix/$p is still present after unlinking OpenSSL: $(ls -l "$prefix/$p")"
            exit 1
        fi
    done
    echo "No OpenSSL headers or libraries left in $prefix"
}

cmd_check_cache() {
    local cache="$1" expect="$2" var lib
    for var in OPENSSL_CRYPTO_LIBRARY OPENSSL_SSL_LIBRARY; do
        lib="$(sed -n "s/^$var:FILEPATH=//p" "$cache")"
        echo "$var=$lib"
        case "$lib" in
            "$expect"/lib*) ;;
            *)
                echo "::error::$var is '$lib', not under $expect"
                exit 1
                ;;
        esac
    done
}

cmd_check_link() {
    local build="$1" expect="$2" refs ref target bad=0
    # The bundle (app, frameworks, helpers, nereusd) plus the build's own
    # top-level dylibs and nereusd, before macdeployqt rewrites any path.
    refs="$( { list_machos "$build/NereusSDR.app"
               find "$build" -maxdepth 1 -type f \( -name '*.dylib' -o -name nereusd \); } \
             | while IFS= read -r bin; do openssl_refs "$bin"; done )"
    if [ -z "$refs" ]; then
        echo "::error::No built binary references libcrypto or libssl, so this check proved nothing; has OpenSSL moved?"
        exit 1
    fi
    echo "$refs"
    while IFS= read -r ref; do
        target="${ref##*: }"
        if [ "$target" != "$expect/libcrypto.3.dylib" ] && [ "$target" != "$expect/libssl.3.dylib" ]; then
            echo "::error::$ref is not $expect/libcrypto.3.dylib or libssl.3.dylib"
            bad=1
        fi
    done <<< "$refs"
    exit "$bad"
}

cmd_check_bundle() {
    local app="$1" lib bad
    for lib in libcrypto.3.dylib libssl.3.dylib; do
        if [ ! -f "$app/Contents/Frameworks/$lib" ]; then
            echo "::error::$lib is missing from Contents/Frameworks; the app cannot launch"
            exit 1
        fi
    done
    bad="$(openssl_refs "$app" | grep -E '(/opt/homebrew|/usr/local|openssl-x86_64)/.*lib(crypto|ssl)' || true)"
    if [ -n "$bad" ]; then
        echo "::error::These bundled binaries still reference an OpenSSL library outside the bundle:"
        echo "$bad"
        exit 1
    fi
    echo "Contents/Frameworks carries libcrypto.3 and libssl.3, and nothing references a copy outside the bundle"
}

case "${1:-}" in
    unlink)       cmd_unlink ;;
    check-cache)  cmd_check_cache "$2" "$3" ;;
    check-link)   cmd_check_link "$2" "$3" ;;
    check-bundle) cmd_check_bundle "$2" ;;
    *)
        echo "usage: $0 unlink | check-cache <cache> <lib dir> | check-link <build> <lib dir> | check-bundle <app>" >&2
        exit 2
        ;;
esac

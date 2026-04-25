/*
 * msvc_compat.h — force-included via /FI when building libspecbleach
 * with MSVC. Stubs out GCC/Clang attribute syntax that the upstream
 * source uses unconditionally (no _MSC_VER guards in libspecbleach
 * 41d3f58). Keep this file minimal — every macro here suppresses a
 * compiler diagnostic upstream relied on, so anything we add should
 * be load-bearing for the Windows build only.
 */
#pragma once

#ifdef _MSC_VER
#  ifndef __attribute__
#    define __attribute__(x)
#  endif
#endif

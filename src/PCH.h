#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOMMNOSOUND
#define UNICODE
#define _UNICODE

#include <F4SE/F4SE.h>
#include <REL/Relocation.h>

#include <Windows.h>
// Windows.h defines this name as a macro, which would rewrite
// REX::W32::InterlockedCompareExchange in RE/Havok headers included after it.
#undef InterlockedCompareExchange

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

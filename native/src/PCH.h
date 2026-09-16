#pragma once

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

#include <SimpleIni.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_set>
#include <thread>

namespace logger = SKSE::log;

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;

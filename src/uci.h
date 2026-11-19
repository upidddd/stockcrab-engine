// StockCrab: UCI commands, shared by the native program and the browser build.
#pragma once

#include <functional>
#include <string>

namespace uci {
using Writer = std::function<void(const std::string &)>;
// use_threads: search in a thread of its own (native), or in the caller (the browser's Web Worker)
void init(Writer w, bool use_threads);
bool command(const std::string &line); // false on "quit"
void shutdown();
void run_bench();
const char *name();
} // namespace uci

// StockCrab: the browser build. Runs inside a Web Worker: every message posted to the worker is a UCI command,
// every line the engine prints is posted back, like stockfish.js does.

#include <emscripten.h>
#include <string>

#include "uci.h"

EM_JS(void, post_line, (const char *s), { postMessage(UTF8ToString(s)); });

extern "C" EMSCRIPTEN_KEEPALIVE void stockcrab_command(const char *line) { uci::command(line); }

int main() {
    uci::init([](const std::string &s) { post_line(s.c_str()); }, false);
    // the commands that arrived while the module was loading
    EM_ASM({
        stockcrabReady = true;
        while (stockcrabQueue.length) Module.ccall("stockcrab_command", null, ["string"], [stockcrabQueue.shift()]);
    });
    return 0;
}

CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra -flto
ARCH ?= -march=native
SRC = src/board.cpp src/eval.cpp src/search.cpp src/uci.cpp src/main.cpp

stockcrab: $(SRC) src/*.h
	$(CXX) $(CXXFLAGS) $(ARCH) -o $@ $(SRC) -pthread

# a build that runs on any x86-64 machine with popcount (2008 and newer)
portable: ARCH = -march=x86-64-v2
portable: stockcrab

test: stockcrab
	$(CXX) -O2 -std=c++17 $(ARCH) -o perft-test tests/perft.cpp src/board.cpp && ./perft-test

clean:
	rm -f stockcrab perft-test

.PHONY: test clean portable wasm

# the browser build: one JavaScript file with the WebAssembly inside, run as a Web Worker that speaks UCI
WASM_SRC = src/board.cpp src/eval.cpp src/search.cpp src/uci.cpp src/wasm.cpp
wasm: dist/stockcrab.js
dist/stockcrab.js: $(WASM_SRC) src/*.h wasm/pre.js
	mkdir -p dist
	em++ -O3 -std=c++17 -flto $(WASM_SRC) -o $@ --pre-js wasm/pre.js \
	  -sENVIRONMENT=worker -sSINGLE_FILE=1 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB -sMAXIMUM_MEMORY=1GB \
	  -sEXPORTED_FUNCTIONS=_main,_stockcrab_command -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString -sSTACK_SIZE=2MB

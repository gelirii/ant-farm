CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -O2 -g -std=c++17 -Wall -Wextra -Wpedantic
CORE := src/core.cpp src/weather.cpp src/persistence.cpp
HEADERS := $(wildcard include/antfarm/*.hpp)
.PHONY: all test clean sanitize test-render
all: build/antfarm out
out:
	mkdir -p out
build:
	mkdir -p build out
build/antfarm: $(CORE) src/render.cpp src/main.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) src/render.cpp src/main.cpp -o $@
build/antfarm-reference: $(CORE) src/render.cpp src/main.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DANTFARM_REFERENCE_ACCESS $(CORE) src/render.cpp src/main.cpp -o $@
build/test_core: $(CORE) tests/test_core.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tests/test_core.cpp -o $@
build/inspect: $(CORE) tools/inspect.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) tools/inspect.cpp -o $@
build/test_weather: src/weather.cpp tests/test_weather.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/weather.cpp tests/test_weather.cpp -o $@
build/test_render: $(CORE) src/render.cpp tools/render_check.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CORE) src/render.cpp tools/render_check.cpp -o $@
test: build/test_core build/test_weather
	./build/test_core
	./build/test_weather
test-render: build/test_render
	./build/test_render
sanitize: | build
	$(CXX) $(CPPFLAGS) -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer $(CORE) tests/test_core.cpp -o build/test_sanitize
	./build/test_sanitize
clean:
	rm -rf build

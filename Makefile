.PHONY: build run clean menu

build/CMakeCache.txt:
	cmake -S . -B build
	ln -sf build/compile_commands.json compile_commands.json

configure: build/CMakeCache.txt

build: configure
	cmake --build build

run: build
	./build/main

clean:
	rm -rf build
	rm -f main

menu:
	cmake -S . -B build -DENABLE_VENDOR_TESTS=ON
	ln -sf build/compile_commands.json compile_commands.json
	cmake --build build --target test_ticalcs_2
	./build/tilibs/libticalcs/trunk/tests/test_ticalcs_2 -c SilverLink

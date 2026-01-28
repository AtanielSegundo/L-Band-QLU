include_path := -I ../headers/ -I ./src/ -I ./includes

release_flags := -O3 -march=native
debug_flags   := -Wall -pedantic -Wextra -O0 -g 

target  ?= _release

ifeq ($(target),_release)
	build_flags = $(release_flags)
else
	build_flags = $(debug_flags)
endif

build: build/main.exe

build/main.exe : src/main.c ../headers/complex_bpsk.h includes/mod_configs.h
	gcc $< -o $@ $(include_path) $(build_flags)
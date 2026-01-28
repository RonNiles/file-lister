FEATURE_FLAGS := -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -std=c++20
WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wduplicated-branches -Wduplicated-cond -Wlogical-op

ifdef DEBUG
CFLAGS := -O0 -ggdb $(FEATURE_FLAGS) $(WARN_FLAGS)
else
CFLAGS := -O2 -static -DNDEBUG $(FEATURE_FLAGS) $(WARN_FLAGS)
endif

.PHONY: all clean format

all: file-lister file-comparer

file-lister: file-lister.cpp dir_level.cpp dir_level.h
	g++ $(CFLAGS) $^ -o $@

file-comparer: file-comparer.cpp dir_level.cpp dir_level.h
	g++ $(CFLAGS) $^ -o $@

clean:
	rm -f file-lister file-comparer

format:
	clang-format -i -style="{BasedOnStyle: Google, ColumnLimit: 90}" file-lister.cpp file-comparer.cpp
	clang-format -i -style="{BasedOnStyle: Google, ColumnLimit: 90}" dir_level.cpp dir_level.h
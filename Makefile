FEATURE_FLAGS := -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -std=c++20
WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wduplicated-branches -Wduplicated-cond -Wlogical-op

ifdef DEBUG
CFLAGS := -O0 -ggdb $(FEATURE_FLAGS) $(WARN_FLAGS)
else
CFLAGS := -O2 -static -DNDEBUG $(FEATURE_FLAGS) $(WARN_FLAGS)
endif

.PHONY: all clean format

all: file-lister

file-lister: file-lister.cpp
	g++ $(CFLAGS) $< -o $@
clean:
	rm -f file-lister

format:
	clang-format -i -style="{BasedOnStyle: Google, ColumnLimit: 90}" file-lister.cpp

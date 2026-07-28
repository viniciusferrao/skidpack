# skidpack - DSI resource container codec
#
# Strict C89, no dependencies. For a 16-bit DOS build run MSCBUILD.BAT under
# Microsoft C 5.10.
CC      ?= cc
CFLAGS  ?= -std=c89 -pedantic -Wall -Wextra -O2
SRC      = src/buf.c src/vle.c src/rle.c src/cli.c src/sdtitl.c src/modpack.c \
           src/glob.c src/main.c
OBJ      = $(SRC:.c=.o)

skidpack: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(OBJ): src/skidpack.h src/version.h src/cli.h src/sdtitl.h src/modpack.h \
        src/glob.h

# Check against the recorded corpus. DIR holds one directory per release, named
# st10, st11, 4d90 and 4d91:
#   make check DIR=/path/to/releases
check: skidpack
	@cd test && $(CC) $(CFLAGS) -o corpus corpus.c && ./corpus check ../skidpack "$(DIR)"

# Round-trip a directory with no manifest entry, trying both dialects.
#   make sweep DIR=/path/to/stunts
sweep: skidpack
	@cd test && $(CC) $(CFLAGS) -o corpus corpus.c && ./corpus sweep ../skidpack "$(DIR)"/*

# Apply the house style. CLANG_FORMAT lets you point at a pinned build; CI uses
# clang-format-22 and the config is written against that major.
CLANG_FORMAT ?= clang-format

format:
	$(CLANG_FORMAT) -i --style=file src/*.c src/*.h test/corpus.c

format-check:
	$(CLANG_FORMAT) --dry-run --Werror --style=file src/*.c src/*.h test/corpus.c

# What CI runs. Needs cppcheck; the analyzer needs GCC 10 or newer.
lint:
	cppcheck --std=c89 --enable=warning,performance,portability \
	         --inline-suppr --error-exitcode=1 \
	         --suppress=missingIncludeSystem src/ test/corpus.c
	@for f in $(SRC) test/corpus.c; do \
	  $(CC) -std=c90 -pedantic-errors -Wall -Wextra -Wshadow -Wcast-qual \
	        -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings \
	        -fanalyzer -O2 -c $$f -o /dev/null || exit 1; \
	done

# What a release ships to DOS instead of the markdown, which reads badly in
# EDIT.COM. 78 columns, and CRLF because that is what DOS editors expect.
README.TXT: README.md tools/txtify.awk
	awk -f tools/txtify.awk README.md | sed 's/$$/\r/' > $@

clean:
	rm -f $(OBJ) skidpack skidpack.exe README.TXT test/corpus test/corpus.exe

.PHONY: check sweep format lint clean

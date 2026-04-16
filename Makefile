# ─────────────────────────────────────────────────────────────────────
# Schrödinger's Database (SDB) — Makefile
# ─────────────────────────────────────────────────────────────────────

CC       = gcc
CFLAGS   = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -Iinclude
LDFLAGS  =

# Source files
SRCS     = src/record.c src/hashmap.c src/secure_erase.c src/volatility.c \
           src/entropy.c src/commands.c src/aggregate.c src/errors.c \
           src/logging.c
MAIN_SRC = src/main.c

# Object files
OBJS     = $(SRCS:.c=.o)
MAIN_OBJ = $(MAIN_SRC:.c=.o)

# Test — single compilation unit (test_main.c #includes all test files)
TEST_SRC  = tests/test_main.c
TEST_OBJ  = tests/test_main.o

# Outputs
BIN      = sdb
TEST_BIN = sdb_tests

# ── Targets ────────────────────────────────────────────────────────

.PHONY: all clean test debug

all: $(BIN)

$(BIN): $(OBJS) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Compile .c to .o
src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Tests ──────────────────────────────────────────────────────────

test: CFLAGS += -DSDB_TESTING -Itests
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_OBJ): $(TEST_SRC) tests/*.c tests/test_harness.h
	$(CC) $(CFLAGS) -Itests -c -o $@ $<

$(TEST_BIN): $(OBJS) $(TEST_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

# ── Debug build ────────────────────────────────────────────────────

debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean $(BIN)

# ── Clean ──────────────────────────────────────────────────────────

clean:
	rm -f $(OBJS) $(MAIN_OBJ) $(TEST_OBJ) $(BIN) $(TEST_BIN)

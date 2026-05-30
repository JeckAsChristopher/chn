# =============================================================================
# CHN 1.1  Makefile
# Supports: Linux, macOS, Termux/Android (AArch64)
# =============================================================================

CC      = gcc
CXX     = g++

V       = src/v1.1
MEM     = src/memory
INC_MEM = src/memory/include
OBJ     = build
BIN     = chn

# ── Flags ────────────────────────────────────────────────────────────────────
BASE    = -Wall -Wextra -O2 -I$(V) -I$(INC_MEM)
CFLAGS  = $(BASE) -std=c11
CXXFLAGS= $(BASE) -std=c++17
LDFLAGS = -lm

# Optional: uncomment to enable readline in the REPL
# CFLAGS   += -DHAVE_READLINE
# LDFLAGS  += -lreadline

# ── C sources ─────────────────────────────────────────────────────────────────
C_SRCS = \
	$(V)/gc.c           \
	$(V)/error.c        \
	$(V)/lexer.c        \
	$(V)/ast.c          \
	$(V)/func.c         \
	$(V)/native.c       \
	$(V)/native_net_stub.c \
	$(V)/bytecode.c     \
	$(V)/parser.c       \
	$(V)/compiler.c     \
	$(V)/vm.c           \
	$(V)/main.c

# ── C++ sources ───────────────────────────────────────────────────────────────
CXX_SRCS = \
	$(MEM)/MemoryTypes.cpp   \
	$(MEM)/RAII.cpp          \
	$(MEM)/MemoryHandler.cpp \
	$(MEM)/Repl.cpp

C_OBJS   = $(patsubst $(V)/%.c,       $(OBJ)/%.o,      $(C_SRCS))
CXX_OBJS = $(patsubst $(MEM)/%.cpp,   $(OBJ)/mem_%.o,  $(CXX_SRCS))
ALL_OBJS = $(C_OBJS) $(CXX_OBJS)

# =============================================================================
.PHONY: all clean debug release termux test

all: $(OBJ) $(BIN)

$(BIN): $(ALL_OBJS)
	$(CXX) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Built: $@  ($(shell wc -c < $@) bytes)"

$(OBJ)/%.o: $(V)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/mem_%.o: $(MEM)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ):
	mkdir -p $@

# ── Targets ───────────────────────────────────────────────────────────────────
debug:
	$(MAKE) all CFLAGS="$(CFLAGS) -g -O0 -DDEBUG" \
	            CXXFLAGS="$(CXXFLAGS) -g -O0 -DDEBUG"

release:
	$(MAKE) all CFLAGS="$(CFLAGS) -O3 -DNDEBUG -march=native" \
	            CXXFLAGS="$(CXXFLAGS) -O3 -DNDEBUG -march=native"

termux:
	$(MAKE) all CFLAGS="$(CFLAGS) -march=armv8-a" \
	            CXXFLAGS="$(CXXFLAGS) -march=armv8-a"

# ── Tests ─────────────────────────────────────────────────────────────────────
test: $(BIN)
	@echo "=== CHN 1.1 Smoke Tests ==="
	@echo 'entry main() { stdo("Hello!") }' > /tmp/_chn_t.chn && \
	 ./$(BIN) /tmp/_chn_t.chn | grep -q "Hello!" && echo "PASS hello" || echo "FAIL hello"
	@echo 'entry main() { stdo(2+3*4) }' > /tmp/_chn_t.chn && \
	 ./$(BIN) /tmp/_chn_t.chn | grep -q "14" && echo "PASS arithmetic" || echo "FAIL arithmetic"
	@echo 'entry main() { stdo(10/0) }' > /tmp/_chn_t.chn && \
	 ./$(BIN) /tmp/_chn_t.chn 2>&1 | grep -q "ChnArithmeticError" && echo "PASS div-zero error" || echo "FAIL div-zero error"
	@echo 'entry main() { stdo(undef_x) }' > /tmp/_chn_t.chn && \
	 ./$(BIN) /tmp/_chn_t.chn 2>&1 | grep -q "ChnReferenceError" && echo "PASS ref error" || echo "FAIL ref error"
	@rm -f /tmp/_chn_t.chn
	@echo "=== Done ==="

clean:
	rm -rf $(OBJ) $(BIN)
	@echo "Cleaned."

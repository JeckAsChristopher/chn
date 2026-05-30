#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

extern "C" {
#include "../v1.1/common.h"
#include "../v1.1/error.h"
#include "../v1.1/lexer.h"
#include "../v1.1/ast.h"
#include "../v1.1/parser.h"
#include "../v1.1/func.h"
#include "../v1.1/compiler.h"
#include "../v1.1/vm.h"
#include "../v1.1/gc.h"
#include "../v1.1/bytecode.h"
#include "../v1.1/native.h"

extern int func_registry_count;
extern int imported_count;
}

#include "include/MemoryTypes.h"
#include "include/MemoryHandler.h"
#include "include/RAII.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

static std::string history_path() {
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    return std::string(home) + "/.chn_history";
}

static constexpr int kHistoryMax = 500;

struct Depth { int brace, paren, bracket; };

static Depth scan_depth(const std::string &src) {
    Depth d{0, 0, 0};
    bool in_str   = false;
    char str_char = 0;
    bool in_lcomm = false;

    for (std::size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char n = (i + 1 < src.size()) ? src[i + 1] : '\0';

        if (!in_str && c == '-' && n == '-') {
            in_lcomm = true; continue;
        }
        if (in_lcomm) {
            if (c == '\n') in_lcomm = false;
            continue;
        }
        if (!in_str && c == '/' && n == '*') {
            i += 2;
            while (i + 1 < src.size() &&
                   !(src[i] == '*' && src[i+1] == '/')) ++i;
            i += 1;
            continue;
        }
        if (!in_str && (c == '"' || c == '\'')) {
            in_str = true; str_char = c; continue;
        }
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == str_char) in_str = false;
            continue;
        }
        switch (c) {
            case '{': d.brace++;   break;
            case '}': d.brace--;   break;
            case '(': d.paren++;   break;
            case ')': d.paren--;   break;
            case '[': d.bracket++; break;
            case ']': d.bracket--; break;
            default: break;
        }
    }
    return d;
}

static bool read_line(const char *prompt, std::string &out) {
#ifdef HAVE_READLINE
    char *line = readline(prompt);
    if (!line) { std::printf("\n"); return false; }
    out = line;
    std::free(line);
    return true;
#else
    std::printf("%s", prompt); std::fflush(stdout);
    char buf[65536];
    if (!std::fgets(buf, sizeof(buf), stdin)) { std::printf("\n"); return false; }
    out = buf;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return true;
#endif
}

static std::vector<std::string> g_history;

static void history_load() {
    std::string path = history_path();
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char buf[65536];
    while (std::fgets(buf, sizeof(buf), f)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty()) g_history.push_back(line);
    }
    std::fclose(f);
    if ((int)g_history.size() > kHistoryMax)
        g_history.erase(g_history.begin(),
                        g_history.begin() + (int)g_history.size() - kHistoryMax);
#ifdef HAVE_READLINE
    for (const auto &h : g_history) add_history(h.c_str());
#endif
}

static void history_save() {
    std::string path = history_path();
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) return;
    int start = (int)g_history.size() > kHistoryMax
                    ? (int)g_history.size() - kHistoryMax : 0;
    for (int i = start; i < (int)g_history.size(); ++i)
        std::fprintf(f, "%s\n", g_history[i].c_str());
    std::fclose(f);
}

static void history_push(const std::string &line) {
    if (line.empty()) return;
    if (!g_history.empty() && g_history.back() == line) return;
    g_history.push_back(line);
#ifdef HAVE_READLINE
    add_history(line.c_str());
#endif
}

static void print_banner() {
    std::printf(
        "CHN 1.1  Interactive REPL\n"
        "Type :help for commands. :q or Ctrl-D to exit.\n\n");
}

static void cmd_help() {
    std::printf(
        "\nCHN REPL Commands\n"
        "  :q  exit  quit       exit the REPL\n"
        "  :help                show this help\n"
        "  :gc                  GC statistics\n"
        "  :mem                 full memory report\n"
        "  :vars                list global variable slots\n"
        "  :clear               clear the screen\n"
        "  :reset               reset VM state\n"
        "  :time                toggle execution timing\n"
        "  :load <file>         run a .chn file\n"
        "  :ast <code>          dump AST for a snippet\n"
        "  :dis <code>          disassemble bytecode for a snippet\n\n"
        "  Multi-line input: leave { ( [ open, press Enter to continue.\n"
        "  The prompt changes to ...> until all brackets are closed.\n\n");
}

static void cmd_gc_brief() {
    auto s = chn::MemoryHandler::stats();
    char b1[32], b2[32];
    auto fmt = [](std::size_t b, char *buf, int n) -> const char * {
        if      (b >= 1024*1024) std::snprintf(buf,n,"%.2f MB",b/1048576.0);
        else if (b >= 1024)      std::snprintf(buf,n,"%.2f KB",b/1024.0);
        else                     std::snprintf(buf,n,"%zu B",b);
        return buf;
    };
    std::printf(
        "GC  live %s  |  minor %d  major %d  |  str %d  arr %d  dict %d\n",
        fmt(s.bytes_live, b1, sizeof b1),
        s.minor_collections,
        s.major_collections,
        s.live_strings, s.live_arrays, s.live_dicts);
    (void)b2;
}

static bool g_timing = false;

static void cmd_time_toggle() {
    g_timing = !g_timing;
    std::printf("Timing %s\n", g_timing ? "on" : "off");
}

static void cmd_clear() {
    std::printf("\n\n\n\n\n");
    std::fflush(stdout);
}

static void cmd_vars() {
    std::printf("Globals  use :dis to inspect compiled bytecode\n");
}

extern "C" bool compile_source(const char *filepath, const char *source,
                                Compiler *C);

static void execute(const std::string &src, bool ast_mode, bool dis_mode) {
    chn::MallocGuard dup_guard(strdup(src.c_str()));
    char *dup = dup_guard.chars();

    gc_init();
    auto gc_cleanup = chn::defer([&]{ gc_free_all(); });

    if (ast_mode) {
        error_init("<repl>", dup);
        Parser P; parser_init(&P, dup);
        ASTNode *ast = parser_parse(&P);
        if (!g_had_error) ast_print(ast, 0);
        ast_free(ast);
        return;
    }

    Compiler C;
    C.opt_level = 2;
    if (!compile_source("<repl>", dup, &C)) return;

    chn::ChunkGuard chunk(
        reinterpret_cast<chn::OpaqueChunk *>(&C.top_chunk));

    if (dis_mode) {
        chunk_disasm(&C.top_chunk, "<repl>");
        return;
    }

    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();

    VM vm; vm_init(&vm);
    vm.global_count = C.top_chunk.var_count;

    chn::VMGuard vm_guard(reinterpret_cast<chn::OpaqueVM *>(&vm));

    try {
        VMResult res = vm_run(&vm, &C.top_chunk);
        auto t1 = Clock::now();
        if (g_timing) {
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::printf("  %.3f ms\n", ms);
        }
        (void)res;
    } catch (const chn::ChnMemoryError &e) {
        std::fprintf(stderr, "\nChnMemoryError: %s\n", e.what());
    }
}

static void execute_file(const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "ChnImportError: cannot open '%s'\n", path);
        return;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    chn::MallocGuard buf_guard(std::malloc(static_cast<std::size_t>(sz) + 1));
    if (!buf_guard.get()) { std::fclose(f); return; }
    char *buf = buf_guard.chars();
    std::size_t n = std::fread(buf, 1, static_cast<std::size_t>(sz), f);
    buf[n] = '\0';
    std::fclose(f);

    execute(std::string(buf), false, false);
}

static std::string make_prompt(const Depth &d) {
    if (d.brace == 0 && d.paren == 0 && d.bracket == 0)
        return "chn> ";

    char badge[32];
    std::snprintf(badge, sizeof(badge), " %d",
                  d.brace + d.paren + d.bracket);
    return std::string("...") + badge + "> ";
}

extern "C" void chn_repl_run(void) {
    print_banner();
    history_load();

    std::string multi_buf;
    Depth accumulated{0,0,0};

    auto history_save_guard = chn::defer([]{ history_save(); });

    for (;;) {
        std::string line;
        std::string prompt = make_prompt(accumulated);

        if (!read_line(prompt.c_str(), line)) break;

        if (line == ":q" || line == "quit" || line == "exit") break;

        if (multi_buf.empty()) {
            if (line == ":help") { cmd_help();        continue; }
            if (line == ":gc")   { cmd_gc_brief();    continue; }
            if (line == ":mem")  { chn_mem_report();  continue; }
            if (line == ":vars") { cmd_vars();         continue; }
            if (line == ":clear"){ cmd_clear();        continue; }
            if (line == ":time") { cmd_time_toggle();  continue; }
            if (line == ":reset") {
                multi_buf.clear();
                accumulated = {0,0,0};
                gc_free_all(); gc_init();
                std::printf("Reset.\n");
                continue;
            }
            if (line.substr(0, 6) == ":load ") {
                std::string path = line.substr(6);
                while (!path.empty() && path.front() == ' ') path.erase(path.begin());
                if (path.empty())
                    std::fprintf(stderr, "usage: :load <file.chn>\n");
                else
                    execute_file(path.c_str());
                continue;
            }
            if (line.substr(0, 5) == ":ast ") {
                std::string code = line.substr(5);
                history_push(line);
                execute(code, true, false);
                continue;
            }
            if (line.substr(0, 5) == ":dis ") {
                std::string code = line.substr(5);
                history_push(line);
                execute(code, false, true);
                continue;
            }
            if (line.empty()) continue;
        }

        if (!multi_buf.empty()) multi_buf += '\n';
        multi_buf += line;

        Depth d = scan_depth(multi_buf);

        if (d.brace > 0 || d.paren > 0 || d.bracket > 0) {
            accumulated = d;
            continue;
        }

        if (d.brace < 0 || d.paren < 0 || d.bracket < 0) {
            std::fprintf(stderr,
                "ChnSyntaxError: mismatched bracket "
                "({ } depth %d  ( ) depth %d  [ ] depth %d)\n",
                d.brace, d.paren, d.bracket);
            multi_buf.clear();
            accumulated = {0,0,0};
            continue;
        }

        std::string src = multi_buf;
        multi_buf.clear();
        accumulated = {0,0,0};

        history_push(src);
        execute(src, false, false);
    }

    std::printf("\nBye!\n");
}

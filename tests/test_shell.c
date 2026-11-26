#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { OUTPUT_SIZE = 65536, TIMEOUT_MS = 8000 };
typedef struct { int status; char out[OUTPUT_SIZE], err[OUTPUT_SIZE]; } Result;
typedef struct { pid_t pid; int fd; char pending[OUTPUT_SIZE]; } Terminal;

static void fatal(const char *what) { perror(what); exit(2); }
static long long milliseconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) fatal("clock_gettime");
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void pause_ms(long ms) {
    struct timespec t = {ms / 1000, ms % 1000 * 1000000};
    while (nanosleep(&t, &t) < 0) if (errno != EINTR) fatal("nanosleep");
}
static void write_all(int fd, const char *s) {
    size_t left = strlen(s);
    while (left) {
        ssize_t n = write(fd, s, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) fatal("write");
        s += n; left -= (size_t)n;
    }
}
static int temporary(void) {
    char name[] = "/tmp/mysh-capture-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0 || unlink(name) < 0) fatal("temporary file");
    return fd;
}
static void capture(int fd, char *buffer) {
    if (lseek(fd, 0, SEEK_SET) < 0) fatal("lseek");
    size_t used = 0;
    while (used < OUTPUT_SIZE - 1) {
        ssize_t n = read(fd, buffer + used, OUTPUT_SIZE - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) fatal("read");
        if (!n) break;
        used += (size_t)n;
    }
    buffer[used] = 0;
    if (close(fd) < 0) fatal("close");
}
static Result run(const char *program, const char *command,
                  const char *input, const char *option) {
    int in = temporary(), out = temporary(), err = temporary();
    if (input) write_all(in, input);
    if (lseek(in, 0, SEEK_SET) < 0) fatal("lseek");
    pid_t pid = fork();
    if (pid < 0) fatal("fork");
    if (!pid) {
        if (setpgid(0, 0) < 0 || dup2(in, 0) < 0 || dup2(out, 1) < 0 ||
            dup2(err, 2) < 0) _exit(125);
        close(in); close(out); close(err);
        if (option) execl(program, program, option, "-c", command, (char *)NULL);
        else if (command) execl(program, program, "-c", command, (char *)NULL);
        else execl(program, program, (char *)NULL);
        _exit(125);
    }
    if (close(in) < 0) fatal("close");
    int status = 0;
    long long deadline = milliseconds() + TIMEOUT_MS;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0 && errno != EINTR) fatal("waitpid");
        if (milliseconds() >= deadline) {
            (void)kill(-pid, SIGKILL); (void)kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) fatal("waitpid");
            break;
        }
        pause_ms(5);
    }
    Result r = {.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)};
    capture(out, r.out); capture(err, r.err);
    return r;
}
static int check(const char *name, Result r, int status, const char *output) {
    int ok = r.status == status && (!output || !strcmp(r.out, output));
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fprintf(stderr, "  status=%d expected=%d\n  stdout: %s\n  stderr: %s\n",
                     r.status, status, r.out, r.err);
    return !ok;
}
static int expect(const char *bin, const char *command, int status, const char *out) {
    return check(command, run(bin, command, NULL, NULL), status, out);
}
static int equivalent(const char *bin, const char *command) {
    Result reference = run("/bin/bash", command, NULL, NULL);
    return check(command, run(bin, command, NULL, NULL), reference.status, reference.out);
}

static int until(Terminal *t, const char *marker, char *output) {
    long long deadline = milliseconds() + TIMEOUT_MS;
    for (;;) {
        char *end = strstr(t->pending, marker);
        if (end) {
            size_t count = (size_t)(end - t->pending) + strlen(marker);
            memcpy(output, t->pending, count); output[count] = 0;
            memmove(t->pending, t->pending + count, strlen(t->pending + count) + 1);
            return 1;
        }
        long long remaining = deadline - milliseconds();
        if (remaining <= 0) break;
        struct pollfd p = {t->fd, POLLIN, 0};
        int ready = poll(&p, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) break;
        size_t used = strlen(t->pending);
        if (used == sizeof(t->pending) - 1) break;
        ssize_t n = read(t->fd, t->pending + used, sizeof(t->pending) - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        t->pending[used + (size_t)n] = 0;
    }
    fprintf(stderr, "PTY: expected [%s], received [%s]\n", marker, t->pending);
    return 0;
}
static int terminal_test(const char *bin, int scenario) {
    Terminal t = {.fd = -1};
    t.pid = forkpty(&t.fd, NULL, NULL, NULL);
    if (t.pid < 0) fatal("forkpty");
    if (!t.pid) { execl(bin, bin, (char *)NULL); _exit(125); }
    char output[OUTPUT_SIZE];
    int ok = until(&t, "mysh$ ", output);
#define SEND(s) do { if (ok) write_all(t.fd, (s)); } while (0)
#define READ(marker) do { if (ok) ok = until(&t, (marker), output); } while (0)
#define CONTAINS(s) do { if (ok && !strstr(output, (s))) ok = 0; } while (0)
    if (scenario == 0 || scenario == 1) {
        SEND(scenario == 0 ? "sleep 30\n" : "sleep 30 | cat\n");
        pause_ms(150); SEND("\032"); READ("mysh$ "); CONTAINS("Stopped");
        if (scenario == 0) {
            SEND("bg %+\n"); READ("mysh$ "); CONTAINS("Running");
            SEND("fg %+\n"); pause_ms(150); SEND("\003"); READ("mysh$ ");
        } else {
            SEND("kill -KILL %+\n"); READ("mysh$ ");
            pause_ms(150);
            SEND("wait\n"); READ("mysh$ ");
        }
    } else if (scenario == 2) {
        SEND("echo 'hello\n"); READ("> ");
        SEND("world'\n"); READ("mysh$ "); CONTAINS("hello\r\nworld");
        SEND("echo |\n"); READ("mysh$ "); CONTAINS("mysh: syntax:");
    } else if (scenario == 3) {
        SEND("\003"); READ("mysh$ ");
    } else {
        const char *commands[] = {"echo first\n", "command_that_does_not_exist_123\n",
                                 "cd /directory_that_does_not_exist_123\n", "echo |\n"};
        for (size_t i = 0; i < sizeof(commands) / sizeof(*commands); ++i) {
            SEND(commands[i]); READ("mysh$ ");
        }
    }
    SEND("echo alive\n"); READ("mysh$ "); CONTAINS("alive\r\n");
    SEND("exit\n");
    int status = 0; long long deadline = milliseconds() + TIMEOUT_MS;
    size_t tail = 0; output[0] = 0;
    for (;;) {
        struct pollfd p = {t.fd, POLLIN, 0};
        int ready = poll(&p, 1, 0);
        if (ready < 0 && errno != EINTR) fatal("poll");
        if (ready > 0 && (p.revents & POLLIN) && tail < sizeof(output) - 1) {
            ssize_t n = read(t.fd, output + tail, sizeof(output) - tail - 1);
            if (n > 0) { tail += (size_t)n; output[tail] = 0; }
            else if (n < 0 && errno != EINTR && errno != EIO) fatal("read PTY");
        }
        pid_t done = waitpid(t.pid, &status, WNOHANG);
        if (done == t.pid) break;
        if (done < 0 && errno != EINTR) fatal("waitpid");
        if (!ok || milliseconds() >= deadline) {
            (void)kill(t.pid, SIGKILL);
            while (waitpid(t.pid, &status, 0) < 0) if (errno != EINTR) fatal("waitpid");
            ok = 0; break;
        }
        pause_ms(5);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ok = 0;
    if (!ok) fprintf(stderr, "PTY exit status=%d, diagnostics: %s\n", status, output);
    if (close(t.fd) < 0) fatal("close PTY");
    const char *names[] = {"stop/bg/fg/Ctrl-C", "pipeline process group", "quotes and recovery",
                           "idle Ctrl-C", "main loop after failures"};
    printf("%s PTY %s\n", ok ? "PASS" : "FAIL", names[scenario]);
    return !ok;
#undef SEND
#undef READ
#undef CONTAINS
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--count-fds")) {
        DIR *d = opendir("/proc/self/fd");
        if (!d) fatal("opendir");
        unsigned count = 0; struct dirent *entry;
        while ((entry = readdir(d))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
        if (closedir(d) < 0) fatal("closedir");
        printf("%u\n", count); return 0;
    }
    char bin[PATH_MAX], self[PATH_MAX];
    if (argc > 2) { fprintf(stderr, "usage: %s [./mysh]\n", argv[0]); return 2; }
    if (!realpath(argc == 2 ? argv[1] : "./mysh", bin) || !realpath(argv[0], self)) fatal("realpath");
    char directory[] = "/tmp/mysh-tests-XXXXXX";
    if (!mkdtemp(directory) || chdir(directory) < 0) fatal("test directory");
    int failed = 0;
    const char *same[] = {
        "printf '<%s>\\n' a\"b c\"d '' 'x|y'", "echo \"a\\tb\"", "echo a\\ b \\| \\#",
        "echo 'a\\b'", "echo abc#def # comment", "echo \"a\\\"b\" \"a\\\\b\"",
        "false && echo no || echo yes", "true || echo no && echo yes", "false; true; false",
        "true && false || true", "(false || echo yes); echo end", "echo a=b",
        "seq 1 200000 | wc -l", "yes x | head -n 1", "echo hello | cat | cat | wc -c",
        "false | true", "true | false", "pwd; (cd /; pwd); pwd; cd / | cat; pwd",
        "exit 300", "false; exit",
        "echo ab\\\ncd", "true &\\\n& echo joined", "false |\\\n| echo fallback",
        "echo -n > out; echo joined >\\\n> out; cat out", "echo \\\n# comment\necho next",
        "echo \"\"#tail", "echo \\2>out; cat out", "echo a\"\"b",
        "echo one;\n\necho two", "echo x &\nwait"
    };
    for (size_t i = 0; i < sizeof(same) / sizeof(*same); ++i) failed += equivalent(bin, same[i]);
    failed += expect(bin, "echo \"$HOME\"", 0, "$HOME\n");
    failed += expect(bin, "echo a > first > second; echo b >> second; cat < second; echo c", 0, "a\nb\nc\n");
    failed += expect(bin, "cat first", 0, "");
    failed += expect(bin, "cat second", 0, "a\nb\n");
    failed += expect(bin, "> out echo hi; cat out", 0, "hi\n");
    Result r = run(bin, "echo no > missing/out", NULL, NULL);
    failed += check("redirection failure", r, 1, "");
    if (strncmp(r.err, "mysh:", 5)) { puts("FAIL diagnostic prefix"); ++failed; }
    failed += expect(bin, "(echo a; (echo b)) > out; cat out", 0, "a\nb\n");
    failed += expect(bin, "command_that_does_not_exist_123", 127, "");
    int fd = open("noexec", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) fatal("open noexec");
    write_all(fd, "echo no\n"); if (close(fd) < 0) fatal("close");
    failed += expect(bin, "./noexec", 126, "");
    if (chmod("noexec", 0700) < 0) fatal("chmod");
    failed += expect(bin, "./noexec", 126, "");
    failed += expect(bin, "./", 126, "");
    failed += expect(bin, "sh -c 'kill -TERM $$'", 143, "");
    const char *invalid[] = {"echo must_not_run; | cat", "echo hi &&", "echo hi >", "echo hi 2> out",
        "echo hi >> > out", "echo $HOME", "echo *.c", "X=\"hello\" echo x",
        "echo hi; if true; then echo x; fi", "echo hi << END", "echo hi &> out", "echo hi |& cat",
        "(echo x", "echo hi > /dev/tcp/host/80", "echo hi;; echo x",
        "X='hello' echo x", "echo must_not_run; echo ~", "echo must_not_run; echo `pwd`"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) failed += expect(bin, invalid[i], 2, "");
    failed += check("stdin continuation", run(bin, NULL, "echo a\\\nb\necho c\n", NULL), 0, "ab\nc\n");
    failed += check("stdin unterminated quote", run(bin, NULL, "echo 'bad\necho no\n", NULL), 2, "");
    failed += check("stdin syntax stops", run(bin, NULL, "echo ok\necho >\necho no\n", NULL), 2, "ok\n");
    failed += expect(bin, "sh -c 'exit 7' & wait %1", 7, "");
    failed += expect(bin, "sleep 5 & kill -TERM %1; wait %1", 143, "");
    failed += expect(bin, "sleep 0.1 && echo later & echo now; wait", 0, "now\nlater\n");
    failed += expect(bin, "echo first; sleep 0.1 & echo second; wait", 0, "first\nsecond\n");
    failed += expect(bin, "fg", 1, ""); failed += expect(bin, "bg", 1, "");
    const char *options[] = {"--dump-tokens", "--dump-ast"}, *markers[] = {"WORD", "PIPE"};
    for (size_t i = 0; i < 2; ++i) {
        r = run(bin, "echo hi | cat", NULL, options[i]);
        failed += check(options[i], r, 0, NULL);
        if (!strstr(r.out, markers[i])) { puts("FAIL dump content"); ++failed; }
    }
    char probe[PATH_MAX + 32], command[PATH_MAX * 2 + 4096];
    if (strchr(self, '\'')) fatal("test runner path contains quote");
    snprintf(probe, sizeof(probe), "'%s' --count-fds", self);
    snprintf(command, sizeof(command), "%s; ", probe);
    for (int i = 0; i < 150; ++i) strcat(command, "echo x > out; ");
    strcat(command, probe); r = run(bin, command, NULL, NULL);
    unsigned a = 0, b = 0;
    int stable = r.status == 0 && sscanf(r.out, "%u\n%u", &a, &b) == 2 && a == b;
    printf("%s descriptor count\n", stable ? "PASS" : "FAIL"); failed += !stable;
    for (int i = 0; i < 5; ++i) failed += terminal_test(bin, i);
    const char *files[] = {"first", "second", "out", "noexec"};
    for (size_t i = 0; i < sizeof(files) / sizeof(*files); ++i)
        if (unlink(files[i]) < 0 && errno != ENOENT) fatal("unlink");
    if (chdir("/") < 0 || rmdir(directory) < 0) fatal("cleanup");
    printf("\n%s: %d failed checks\n", failed ? "FAILED" : "OK", failed);
    return failed ? 1 : 0;
}

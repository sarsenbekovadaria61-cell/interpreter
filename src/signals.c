#include "signals.h"
#include "util.h"
#include <errno.h>
#include <signal.h>
static volatile sig_atomic_t child_changed;
static volatile sig_atomic_t prompt_interrupted;
static void interrupt_prompt(int sig) {
    int saved = errno; (void)sig; prompt_interrupted = 1; errno = saved;
}
static void changed(int sig) {
    int saved = errno; (void)sig; child_changed = 1; errno = saved;
}
static int install(int sig, void (*handler)(int), int flags) {
    struct sigaction a = {0}; a.sa_handler = handler; a.sa_flags = flags;
    if (sigemptyset(&a.sa_mask) < 0 || sigaction(sig, &a, NULL) < 0) { syserr("sigaction"); return -1; }
    return 0;
}
int signals_shell(int interactive) {
    if (install(SIGCHLD, changed, 0) < 0) return -1;
    if (interactive) {
        const int sigs[] = {SIGINT,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
        for (size_t i=0; i<sizeof(sigs)/sizeof(*sigs); ++i)
            if (install(sigs[i], SIG_IGN, 0) < 0) return -1;
        if (install(SIGINT, interrupt_prompt, 0) < 0) return -1;
    }
    return 0;
}
int signals_child(void) {
    const int sigs[] = {SIGINT,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGCHLD,SIGPIPE};
    for (size_t i=0; i<sizeof(sigs)/sizeof(*sigs); ++i)
        if (install(sigs[i], SIG_DFL, 0) < 0) return -1;
    return 0;
}
int signals_pending(void) { int value = child_changed; child_changed = 0; return value; }
int signals_interrupted(void) { int value = prompt_interrupted; prompt_interrupted = 0; return value; }

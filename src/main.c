#include "exec.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"
#include "util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int process(Shell *sh, const char *text, int dump, int allow_more) {
    Tokens tokens; int r=lex(text,&tokens);
    if (r==1 && allow_more) return -1;
    if (r) {
        if (r==1) error("syntax","unterminated quote or escape");
        sh->status=2; return 2;
    }
    if (dump==1) { tokens_dump(&tokens); tokens_free(&tokens); return 0; }
    int status; Node *n=parse(&tokens,&status); tokens_free(&tokens);
    if (status) { sh->status=status; return status; }
    if (dump==2) node_dump(n,0); else if (n) (void)execute(sh,n);
    node_free(n); return 0;
}
int main(int argc, char **argv) {
  
    if (setvbuf(stdin,NULL,_IONBF,0)!=0) { error("stdin","cannot disable buffering"); return 1; }
    const char *command=NULL; int dump=0;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--dump-tokens")) dump=1;
        else if (!strcmp(argv[i],"--dump-ast")) dump=2;
        else if (!strcmp(argv[i],"-c") && i+1<argc) command=argv[++i];
        else { error("usage","mysh [--dump-tokens|--dump-ast] [-c command]"); return 2; }
    }
    Shell sh={0}; int interactive=!command && isatty(STDIN_FILENO) && !dump;
    if (jobs_init(&sh.jobs,interactive)<0) return 1;
    if (command) (void)process(&sh,command,dump,0);
    else {
        char *line=NULL,*logical=xstrdup(""); size_t capacity=0, used=0;
        for (;;) {
            if (sh.exiting) break;
            jobs_poll(&sh.jobs,interactive);
            if (interactive) { fputs(used?"> ":"mysh$ ",stdout); if (fflush(stdout)==EOF) { syserr("stdout"); sh.status=1; break; } }
            ssize_t length;
            for (;;) {
                errno=0; length=getline(&line,&capacity,stdin);
                if (length<0 && errno==EINTR) {
                    clearerr(stdin); jobs_poll(&sh.jobs,0);
                    if (signals_interrupted() && interactive) {
                        used=0; logical[0]=0; sh.status=130;
                        fputs("\nmysh$ ",stdout);
                        if (fflush(stdout)==EOF) { syserr("stdout"); sh.exiting=1; sh.status=1; break; }
                    }
                    continue;
                }
                break;
            }
            if (length<0) {
                if (ferror(stdin)) { syserr("input"); sh.status=1; }
                else if (used) (void)process(&sh,logical,dump,0);
                if (interactive) putchar('\n');
                break;
            }
            logical=xrealloc(logical,used+(size_t)length+1);
            memcpy(logical+used,line,(size_t)length+1); used+=(size_t)length;
            int escaped_newline=used>=2 && logical[used-2]=='\\' && logical[used-1]=='\n';
            int result=process(&sh,logical,dump,interactive || escaped_newline);
            if (result==-1) continue;
            used=0; logical[0]=0;
            if (result==2 && !interactive) break;
        }
        free(line); free(logical);
    }
    jobs_dispose(&sh.jobs,1);
    if (fflush(stdout)==EOF) { syserr("stdout"); sh.status=1; }
    if (close_fd(STDIN_FILENO)<0) sh.status=1;
    if (close_fd(STDOUT_FILENO)<0) sh.status=1;
    return sh.status;
}

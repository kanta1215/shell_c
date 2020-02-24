// 61706589 口井敢太

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAXBUF 1024
#define MAXPATH 50
#define MAXARG 16
#define ENDWORD "exit"
#define TOKENNUM 6
#define TKN_EOF 0
#define TKN_EOL 1
#define TKN_BG 2
#define TKN_PIPE 3
#define TKN_REDIR_IN 4
#define TKN_REDIR_OUT 5
#define TKN_REDIR_APPEND 6
#define TKN_NORMAL 7

struct av_token{
    char *av;
    int token;
};
struct token_table{
    char command;
    int token;
};
struct token_table tkn_tbl[] = {
    {EOF, TKN_EOF},
    {'\n', TKN_EOL},
    {'&', TKN_BG},
    {'|', TKN_PIPE},
    {'<', TKN_REDIR_IN},
    {'>', TKN_REDIR_OUT},
};
void getargs(char *buf, int *argc, char *av[]);
int mygetenv(char *buf, char *env[]);
void bg_wait();

int bgcount = 0, bg_pid[MAXARG], zpid, status;

// 入力解析
void getargs(char *buf, int *argc, char *av[])
{
    int c, i, count = 0;
    char *p = buf;
    *p = '\0';
    
    for (; ;) {
        if ((c = getchar()) == '\n')
            break;
        else 
            ungetc(c, stdin);

        while (isblank(c = getchar()));
        if (count >= MAXARG) {
            while (getchar() != '\n');
            break;
        }
        if (c == '&' || c == '|' || c == '<' || c == '>') {
            for (i = 0; i < TOKENNUM; i++) {
                if (c == tkn_tbl[i].command) {
                    *p = c;
                    av[count] = p;
                    if (i == 5) {
                        if ((c = getchar()) == '>') {
                            p++;
                            *p = c; 
                        }else {
                            ungetc(c, stdin);
                        }
                    }
                    p++;
                    *p = '\0';
                    p++;
                    count++;
                }
            }
        }else {
            ungetc(c, stdin);
            for (i = 0; ; i++) {
                c = getchar();
                if (c != EOF && c != '\n' && c != '&' && c != '|' && c != '<' && c != '>' && !isblank(c)) {
                    *p = c;
                    if (i == 0) {
                        av[count] = p;
                        count++;
                    }
                    p++;
                }else {
                    break;
                }
            }
            ungetc(c, stdin);
            *p = '\0';
            p++;
        }
    }
    *argc = count;
}

// 環境変数の取得
int mygetenv(char *buf, char *env[])
{
    char b[MAXBUF];
    memset(b, 0, sizeof(b));

    if ((buf = getenv("PATH")) == NULL) {
        perror("getenv");
        return -1;
    }
    int i;
    for (i = 0; *buf != '\0'; buf++) {
        if (i == 0) {
            env[i] = buf;
            i++;
        }
        if (*buf == ':') {
            *buf = '\0';
            env[i] = (buf + 1);
            i++;
        }
    }
    return i;
}

// bg_handler
void bg_wait()
{
    while ((zpid = waitpid(-1, &status, WNOHANG)) > 0)
        fprintf(stderr, "[%d] %d terminated\n", bgcount, bg_pid[bgcount - 1]);
    if (zpid == -1 && errno != ECHILD) {
        perror("waitpid");
        exit(1);
    }
}

int main()
{
    char buf[MAXBUF], buf_env[MAXBUF], *env[MAXARG], *av[MAXARG], *av2[MAXARG], *av3[MAXARG];
    char *outfile, *infile, *appfile;
    char *home, *path;
    char filename[MAXPATH], cwd[MAXPATH];
    int pid[MAXARG], fd, envnum = 0, argc = 0, env_flag = 0, pfd[MAXARG][2];
    int i, j, k, count, pipecount, ac;
    int bg_flag = 0, cpgid, fg_fd = open("/dev/tty", O_RDWR);
    extern char **environ;
    struct sigaction act, bg, def;
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    bg.sa_handler = bg_wait;
    sigemptyset(&bg.sa_mask);
    def.sa_handler = SIG_DFL;
    sigemptyset(&def.sa_mask);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTTOU, &act, NULL);

    for (; ;) {
        fprintf(stdout, "$ ");
        memset(av, 0, sizeof(av));
        memset(buf, 0, sizeof(buf));
        argc = 0;
        bg_flag = 0;

        getargs(buf, &argc, av);

        if (argc == 0)
            continue;
        if (strcmp(av[0], "\n") == 0) // 改行無視
            continue;
        if (strcmp(av[0], ENDWORD) == 0) // "exit"でプログラム終了
            exit(0);
        if (argc >= MAXARG) {
            fprintf(stderr, "Too many arguments!\n");
            continue;
        }
        
        // コマンド"cd"の処理(cdは引数の先頭に指定された場合のみ処理可能)
        if (strcmp(av[0], "cd") == 0) {
            if (argc > 2) {
                fprintf(stderr, "Too many arguments!\n");
                fprintf(stderr, "syntax: cd PATH\n");
                continue;
            }else if (argc == 2){
                if (chdir(av[1]) == -1) {
                    perror("chdir");
                    continue;
                }
            }else if (argc == 1) {
                if ((home = getenv("HOME")) == NULL) {
                    perror("getenv");
                    continue;
                }
                if (chdir(home) == -1) {
                    perror("chdir");
                    continue;
                }
            }
        }else {
            // 環境変数, カレントディレクトリのパス取得
            if (env_flag == 0) {
                memset(buf_env, 0, sizeof(buf_env));
                memset(env, 0, sizeof(env));
                memset(cwd, 0, sizeof(cwd));
                if ((envnum = mygetenv(buf_env, env)) == -1)
                    continue;
                if (getcwd(cwd, sizeof(cwd)) == NULL) {
                    perror("getcwd");
                    continue;
                }
                envnum++;
                env[envnum - 1] = cwd;
                env_flag = 1;
            }
            
            pipecount = 0;
            memset(pid, 1, sizeof(pid));
            memset(av2, 0, sizeof(av2));
            infile = outfile = appfile = NULL;
            for (i = 0, k = 0; i < argc; i++) {
                if (strcmp(av[i], ">>") == 0) {
                    appfile = av[i + 1];
                    i++;
                }else if (strcmp(av[i], "<") == 0) {
                    infile = av[i + 1];
                    i++;
                }else if (strcmp(av[i], ">") == 0) {
                    outfile = av[i + 1];
                    i++;
                }else if (strcmp(av[i], "&") == 0) {
                    bg_flag = 1;
                    bgcount++;
                }else if (strcmp(av[i], "|") == 0) {
                    pipe(pfd[pipecount]);
                    pipecount++;
                    if ((pid[pipecount - 1] = fork()) < 0) {
                        perror("fork");
                        exit(1);
                    }
                    if (pid[pipecount - 1] != 0 && pipecount == 1) {
                        if (bg_flag != 1) { // バックグラウンド指定の有無
                            setpgid(pid[pipecount - 1], pid[pipecount - 1]); // プロセスグループ設定
                            tcsetpgrp(fg_fd, pid[pipecount - 1]);
                            cpgid = pid[pipecount - 1];
                        }else {
                            setpgid(pid[pipecount - 1], getpid());
                            bg_pid[bgcount - 1] = pid[pipecount - 1];
                        }
                    }
                    if (pid[pipecount - 1] != 0 && pipecount != 1) {
                        if (bg_flag != 1)
                            setpgid(pid[pipecount - 1], cpgid);
                        else {
                            setpgid(pid[pipecount - 1], getpid());
                            bg_pid[bgcount - 1] = pid[pipecount - 1];
                        }
                    }

                    if (pid[pipecount - 1] == 0) {
                        sigaction(SIGINT, &def, NULL); // シグナル発生時の指定
                        if (pipecount == 1) { // 多段パイプへの対応
                            // ディスクリプタの付け替え
                            dup2(pfd[pipecount - 1][1], 1);
                            close(pfd[pipecount - 1][0]); close(pfd[pipecount - 1][1]);
                        }else {
                            // ディスクリプタの付け替え
                            dup2(pfd[pipecount - 2][0], 0);
                            dup2(pfd[pipecount - 1][1], 1);
                            close(pfd[pipecount - 2][0]); close(pfd[pipecount - 2][1]);
                            close(pfd[pipecount - 1][0]); close(pfd[pipecount - 1][1]);
                        }
                        // リダイレクトの設定
                        if (infile != NULL) {
                            if ((fd = open(infile, O_RDONLY)) == -1) {
                                perror("open");
                                exit(1);
                            }
                            dup2(fd, 0);
                            close(fd);
                        }
                        if (outfile != NULL) {
                            if ((fd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1) {
                                perror("open");
                                exit(1);
                            }
                            dup2(fd, 1);
                            close(fd);
                        }
                        if (appfile != NULL) {
                            if ((fd = open(appfile, O_RDWR|O_CREAT|O_APPEND, 0644)) == -1) {
                                perror("open");
                                exit(1);
                            }
                            dup2(fd, 1);
                            close(fd);
                        }
                        
                        // コマンドをexecveで実行
                        count = 0;
                        for (j = 0; j < envnum; j++) {
                            memset(filename, 0, sizeof(filename));
                            if (snprintf(filename, sizeof(filename), "%s/%s", env[j], av2[0]) < 0) {
                                fprintf(stderr, "Too long PATH\n");
                                exit(1);
                            }
                            if (execve(filename, av2, environ) < 0)
                                count++;
                        }
                        if (count == envnum) {
                            perror("execve");
                            exit(1);
                        }
                    }

                    if (pipecount > 1) {
                        close(pfd[pipecount - 2][0]);
                        close(pfd[pipecount - 2][1]);
                    }
                    memset(av2, 0, sizeof(av2));
                    infile = outfile = appfile = NULL;
                    k = 0;
                    if (!bg_flag) {
                        if (waitpid(pid[pipecount - 1], &status, 0) < 0) {
                            perror("wait(pipe)");
                            exit(1);
                        }
                    }else {
                        sigaction(SIGCHLD, &bg, NULL);
                    }
                    bg_flag = 0;
                }else {
                    av2[k] = av[i];
                    k++;
                }
            }

            if ((pid[pipecount] = fork()) < 0) {
                perror("fork");
                exit(1);
            }
            if (pid[pipecount] != 0 && pipecount == 0) {
                if (bg_flag != 1) {
                    setpgid(pid[pipecount], pid[pipecount]);
                    tcsetpgrp(fg_fd, pid[pipecount]);
                }else {
                    setpgid(pid[pipecount], getpid());
                    bg_pid[bgcount - 1] = pid[pipecount];
                }
            }
            if (pid[pipecount] != 0 && pipecount != 0) {
                if (bg_flag != 1)
                    setpgid(pid[pipecount], cpgid);
                else {
                    setpgid(pid[pipecount], getpid());
                    bg_pid[bgcount - 1] = pid[pipecount];
                }
            }

            if (pid[pipecount] == 0) {
                sigaction(SIGINT, &def, NULL);
                if (pipecount != 0) {
                    dup2(pfd[pipecount - 1][0], 0);
                    close(pfd[pipecount - 1][0]); close(pfd[pipecount - 1][1]);
                }
                
                if (infile != NULL) {
                    if ((fd = open(infile, O_RDONLY)) == -1) {
                        perror("open");
                        exit(1);
                    }
                    dup2(fd, 0);
                    close(fd);
                }
                if (outfile != NULL) {
                    if ((fd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1) {
                        perror("open");
                        exit(1);
                    }
                    dup2(fd, 1);
                    close(fd);
                }
                if (appfile != NULL) {
                    if ((fd = open(appfile, O_RDWR|O_CREAT|O_APPEND, 0644)) == -1) {
                        perror("open");
                        exit(1);
                    }
                    dup2(fd, 1);
                    close(fd);
                }

                count = 0;
                for (i = 0; i < envnum; i++) {
                    memset(filename, 0, sizeof(filename));
                    if (snprintf(filename, sizeof(filename), "%s/%s", env[i], av2[0]) < 0) {
                        fprintf(stderr, "Too long PATH\n");
                        exit(1);
                    }
                    if (execve(filename, av2, environ) < 0)
                        count++;
                }
                if (count == envnum) {
                    perror("execve");
                    exit(1);
                }
            }

            if (pipecount != 0) {
                close(pfd[pipecount - 1][0]);
                close(pfd[pipecount - 1][1]);
            }
            if (bg_flag != 1) {
                if (wait(&status) < 0) {
                    perror("wait");
                    exit(1);
                }
                tcsetpgrp(fg_fd, getpid());
            }else {
                sigaction(SIGCHLD, &bg, NULL);
            }
        }
    }
    exit(0);
}

#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "stdlib.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"

char tempbuf[MAX_LINE_LENGTH * 3];
char buf[MAX_LINE_LENGTH * 3];
int whereToBuffer = 0;
int inputType, foregroundCounter;

void errnoCheck(command *com, int it) {
    if (errno == EACCES) {
        fprintf(stderr, "%s: permission denied\n", (com->redirs)[it]->filename);
    }
    else if (errno == ENOTDIR || errno == ENOENT) {
        fprintf(stderr, "%s: no such file or directory\n", (com->redirs)[it]->filename);
    }
    else {
        fprintf(stderr, "%s: exec error\n", (com->redirs)[it]->filename);
    }
    errno = 0;
}

void errnoCheckSimple(command *com) {
    if (errno == EACCES) {
        fprintf(stderr, "%s: permission denied\n", com->argv[0]);
    }
    else if (errno == ENOTDIR || errno == ENOENT) {
        fprintf(stderr, "%s: no such file or directory\n", com->argv[0]);
    }
    else {
        fprintf(stderr, "%s: exec error\n", com->argv[0]);
    }
    errno = 0;
}


int specialFunctionCheck(command *com) {
    int ibi = 0;
    while (builtins_table[ibi].name != NULL) {
        if (strcmp(builtins_table[ibi].name, com->argv[0]) == 0) {
            (builtins_table[ibi].fun)(com->argv);
            break;
        }
        ibi++;
    }
    if (builtins_table[ibi].name != NULL) return 1;
    return 0;
}

int cidBack[1 << 17];

void sigint_handler(int sig_nb) {
}

void handler(int sig_nb) {
    pid_t child;
    int status;
    do {
        child = waitpid(-1, &status, WNOHANG);
        if (child > 0 && cidBack[child] == 7 && inputType) {
            char toW[80];
            int si = sprintf(toW, status == 0 ? "Background process %d terminated. (exited with status %d)\n"
                                              : "Background process %d terminated. (killed by signal %d)\n", child,
                             status);

            write(1, toW, si);
        }
        if (child > 0 && cidBack[child] == 10)
            foregroundCounter--;
        if (child > 0)
            cidBack[child] = 0;
    } while (child > 0);
}


int tempbufContainsFullLine(int whereTo) {
    int i = 0;
    while (i <= whereTo) {
        if (tempbuf[i] == EOF || tempbuf[i] == 10)
            return 1;
        i++;
    }
    return 0;
}

int minim(int a, int b) {
    return a > b ? b : a;
}

void redirections(command *com) {
    int it = 0;
    while ((com->redirs)[it] != NULL) {

        if ((com->redirs)[it] != NULL && IS_RIN((com->redirs)[it]->flags)) {
            if (close(0)) exit(EXEC_FAILURE);
            int statOpen = (open((com->redirs)[it]->filename, O_RDONLY));
            if (statOpen == -1) {
                errnoCheck(com, it);
            }
            if (statOpen) exit(EXEC_FAILURE);

        }

        if ((com->redirs)[it] != NULL && IS_ROUT((com->redirs)[it]->flags)) {
            if (close(1)) exit(EXEC_FAILURE);
            int statOpen = open((com->redirs)[it]->filename, O_TRUNC | O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
            if (statOpen == -1) {
                errnoCheck(com, it);
                exit(EXEC_FAILURE);
            }

        }

        if ((com->redirs)[it] != NULL && IS_RAPPEND((com->redirs)[it]->flags)) {
            if (close(1)) exit(EXEC_FAILURE);
            int statOpen = open((com->redirs)[it]->filename, O_APPEND | O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
            if (statOpen == -1) {
                errnoCheck(com, it);
                exit(EXEC_FAILURE);
            }
        }
        ++it;
    }
}

void pipeSwitch(int oldFileDescriptor, int newFileDescriptor) {
    if (oldFileDescriptor != newFileDescriptor
        && dup2(oldFileDescriptor, newFileDescriptor) != -1)
        close(oldFileDescriptor);
}

int checkIfPipelineIsValid(pipeline *mpip) {
    int c = 1;
    command **pcmd;

    if (mpip == NULL)
        return 0;

    for (pcmd = mpip; *pcmd; pcmd++, c++)
        if ((*pcmd)->argv[0] == NULL && (c >= 2 || (c == 1 && *(pcmd + 1) != NULL)))
            return 0;


    return 1;
}

int checkIfLineIsValid(line *mline) {
    pipeline *p;
    for (p = mline->pipelines; *p; p++) {
        if (checkIfPipelineIsValid(p) == 0)
            return 0;
    }
    return 1;
}


int
main(int argc, char *argv[]) {
    line *ln;
    command *com;
    int readSize;

    struct stat inputBuffer;


    int inputStatus;
    int flagB, flagBuf = 0;
    int fileDescriptor[2];

    foregroundCounter = 0;
    sigset_t mask, oldmask;
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    sigaction(SIGCHLD, &act, NULL);
    signal(SIGINT, sigint_handler);

    inputStatus = fstat(0, &inputBuffer);
    if (inputStatus == -1) return -1;
    inputType = S_ISCHR(inputBuffer.st_mode);

    while (1) {

        if (inputType) {
            printf(PROMPT_STR);
            fflush(stdout);
        }

        while (whereToBuffer == 0 || tempbufContainsFullLine(minim(MAX_LINE_LENGTH, whereToBuffer) - 1) == 0 ||
               flagBuf == 1) {
            if (whereToBuffer >= MAX_LINE_LENGTH || flagBuf == 1) {   //we have error
                if (flagBuf == 0) {
                    fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
                    flagBuf = 1;
                }

                while (flagBuf == 1 && whereToBuffer != 0) {

                    int iTemp = 0, i2;
                    while (iTemp < whereToBuffer && tempbuf[iTemp] != EOF && tempbuf[iTemp] != 10)
                        iTemp++;
                    if (iTemp == whereToBuffer)
                        whereToBuffer = 0;
                    else {
                        flagBuf = 0;
                        i2 = ++iTemp;
                        while (i2 <= whereToBuffer) {
                            tempbuf[i2 - iTemp] = tempbuf[i2];
                            i2++;
                        }
                        whereToBuffer = i2 - iTemp - 1;
                    }
                }
            }
            if (whereToBuffer <= MAX_LINE_LENGTH && tempbufContainsFullLine(whereToBuffer - 1) == 0) {
                readSize = -1;
                do {
                    readSize = read(0, tempbuf + whereToBuffer, MAX_LINE_LENGTH);
                } while (readSize == -1 && errno == EINTR);
                whereToBuffer += readSize;
                if (readSize <= 0) break;
            }
        }
        if (readSize <= 0) break;

        

        int iForBuffor = 0, iForTempBuf = 0;
        for (; iForBuffor <= whereToBuffer; iForBuffor++) {
            if (tempbuf[iForBuffor] == '#')
                while (tempbuf[iForBuffor] != 10 && tempbuf[iForBuffor] != EOF)
                    iForBuffor++;

            if (tempbuf[iForBuffor] == 10 || tempbuf[iForBuffor] == EOF) {
                buf[iForBuffor] = tempbuf[iForBuffor];
                iForBuffor++;
                buf[iForBuffor] = 0;
                break;
            }
            buf[iForBuffor] = tempbuf[iForBuffor];
        }
       

        for (iForTempBuf = iForBuffor; iForTempBuf <= whereToBuffer; iForTempBuf++)
            tempbuf[iForTempBuf - iForBuffor] = tempbuf[iForTempBuf];
        whereToBuffer = whereToBuffer - iForBuffor; //+1
        
        if (iForBuffor <= 1 || buf[0] == 0 || buf[0] == 10)
            continue;
        ln = parseline(buf);

        if (ln == NULL || checkIfLineIsValid(ln) == 0) {
            fprintf(stderr, SYNTAX_ERROR_STR);
            fprintf(stderr, "\n");
            continue;
        }

        int itLine = 0, itCom, input;
        while (ln->pipelines[itLine] != NULL) {
            flagB = 1;
            pipeline pip = ln->pipelines[itLine++];
            if (ln->flags & LINBACKGROUND) {        //check if command must be done in background
                flagB = 0;
            }

            com = pip[0];
            if (com->argv[0] == NULL || specialFunctionCheck(com)) {
                continue;
            }


            itCom = 0;
            input = STDIN_FILENO;

            while (pip[itCom + 1] != NULL) {
                com = pip[itCom++];
                pipe(fileDescriptor);
                if (flagB)
                    foregroundCounter++;
                int pid_ch = fork();

                cidBack[pid_ch] = !flagB ? 7 : 10;

                if (!pid_ch) {    //child
                    if (!flagB)
                        setsid();
                    close(fileDescriptor[0]);
                    pipeSwitch(input, STDIN_FILENO);    //podmiana wejscia do procesu
                    pipeSwitch(fileDescriptor[1], STDOUT_FILENO); //podmiana wyjscia z procesu
                    redirections(com);
                    execvp(com->argv[0], com->argv);
                }
                else {       //parent
                    if (input != STDIN_FILENO)
                        close(input); //zamykam niepotrzebny dla paranta fd
                    close(fileDescriptor[1]); //zamykam write pipe
                    input = fileDescriptor[0]; //read pipe to moj nowy input
                }
            }

            errno = 0;
            com = pip[itCom++];
            if (flagB)
                foregroundCounter++;
            int k = fork();
            if (!k) {
                if (!flagB)
                    setsid();
                pipeSwitch(input, STDIN_FILENO);    //podmiana wejscia do procesu
                redirections(com);

                execvp(com->argv[0], com->argv);

                errnoCheckSimple(com);     
                exit(EXEC_FAILURE);
            }
            else {
                if (input != STDIN_FILENO)
                    close(input);
                if (flagB) {
                    cidBack[k] = 10;
                    sigemptyset(&mask);
                    sigaddset(&mask, SIGCHLD);

                    sigprocmask(SIG_BLOCK, &mask, &oldmask);
                    while (foregroundCounter > 0)
                        sigsuspend(&oldmask);
                    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);
                }
                else
                    cidBack[k] = 7;
            }
        }

    }

}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

#define MAX_WORDS 64
#define MAX_CMDS 16

void apply_redirect(char **argv) {
    for (int i = 0; argv[i] != NULL; i++) {
        int is_write  = (strcmp(argv[i], ">") == 0);
        int is_append = (strcmp(argv[i], ">>") == 0);
        if (is_write || is_append) {
            if (argv[i + 1] == NULL) {
                fprintf(stderr, "robot: please give a file name after > and >>\n");
                _exit(1);
            }
            int flags = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);
            int fd = open(argv[i + 1], flags, 0644);
            if (fd < 0) {
                perror("open");
                _exit(1);
            }
            dup2(fd, 1);
            close(fd);
            argv[i] = NULL;
            return;
        }
    }
}

void ctrl_c(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

void expand_vars(char **words, int count) {
    for (int i = 0; i < count; i++) {
        if (words[i][0] == '$' && words[i][1] != '\0') {
            const char *value = getenv(words[i] + 1);
            words[i] = (char *)(value ? value : "");
        }
    }
}

int run_pipeline(char **words, int count, int background) {
    char **cmds[MAX_CMDS];
    int ncmds = 0;
    cmds[ncmds++] = &words[0];
    for (int i = 0; i < count; i++) {
        if (strcmp(words[i], "|") == 0) {
            words[i] = NULL;
            if (ncmds == MAX_CMDS) {
                fprintf(stderr, "robot: too many pipes!\n");
                return 1;
            }
            cmds[ncmds++] = &words[i + 1];
        }
    }
    for (int k = 0; k < ncmds; k++) {
        if (cmds[k][0] == NULL) {
            fprintf(stderr, "robot: a command is missing around the |\n");
            return 1;
        }
    }

    pid_t pids[MAX_CMDS];
    int started = 0;
    int failed = 0;
    int prev_read = -1;

    for (int k = 0; k < ncmds; k++) {
        int fd[2] = {-1, -1};
        if (k < ncmds - 1 && pipe(fd) < 0) {
            perror("pipe");
            failed = 1;
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (fd[0] != -1) close(fd[0]);
            if (fd[1] != -1) close(fd[1]);
            failed = 1;
            break;
        }

        if (pid == 0) {
            if (background) signal(SIGINT, SIG_IGN);
            else signal(SIGINT, SIG_DFL);

            if (prev_read != -1) dup2(prev_read, 0);
            if (fd[1] != -1)     dup2(fd[1], 1);
            if (prev_read != -1) close(prev_read);
            if (fd[0] != -1) close(fd[0]);
            if (fd[1] != -1) close(fd[1]);

            apply_redirect(cmds[k]);
            execvp(cmds[k][0], cmds[k]);
            perror("oops");
            _exit(127);
        }

        pids[started++] = pid;
        if (prev_read != -1) close(prev_read);
        if (fd[1] != -1) close(fd[1]);
        prev_read = fd[0];
    }
    if (prev_read != -1) close(prev_read);

    if (background && !failed) {
        printf("[started in background, last helper id %d]\n", pids[started - 1]);
        return 0;
    }

    int result = failed ? 1 : 0;
    for (int k = 0; k < started; k++) {
        int st;
        waitpid(pids[k], &st, 0);
        if (k == started - 1 && !failed) {
            result = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        }
    }
    return result;
}

int main(void) {
    char *line = NULL;
    size_t size = 0;

    printf("Welcome to the shell! Type 'hello' to meet me, 'exit' to make me sleep, 'joke' to hear a random joke or any normal command\n");
    srand(time(NULL));
    signal(SIGINT, ctrl_c);

    while (1) {
        while (waitpid(-1, NULL, WNOHANG) > 0) {}

        printf("robot> ");
        fflush(stdout);
        if (getline(&line, &size, stdin) == -1)
            break;

        char *words[MAX_WORDS];
        int count = 0;
        char *word = strtok(line, " \t\n");
        while (word != NULL && count < MAX_WORDS - 1) {
            words[count++] = word;
            word = strtok(NULL, " \t\n");
        }
        words[count] = NULL;

        if (count == 0) continue;

        expand_vars(words, count);

        if (strcmp(words[0], "hello") == 0) {
            printf("Hello, builder! You made me from scratch. Great job! 🌟\n");
            continue;
        }

        if (strcmp(words[0], "joke") == 0) {
            const char *jokes[] = {
                "Why did the computer go to the doctor? It had a virus!",
                "Why do programmers love nature? Because of all the bugs!",
                "What do you call a sleeping robot? A bot-tom!",
                "Why was the C program sad? It couldn't find its pointer!",
                "How does a robot eat? One byte at a time!"
            };
            int how_many = sizeof(jokes) / sizeof(jokes[0]);
            printf("%s\n", jokes[rand() % how_many]);
            continue;
        }

        if (strcmp(words[0], "exit") == 0) break;

        if (strcmp(words[0], "cd") == 0) {
            const char *place = words[1];
            if (place == NULL) {
                place = getenv("HOME");
            }
            if (chdir(place) != 0) {
                perror("cd");
            }
            continue;
        }

        int background = 0;
        if (strcmp(words[count - 1], "&") == 0) {
            background = 1;
            words[--count] = NULL;
            if (count == 0) continue;
        }

        run_pipeline(words, count, background);
    }

    free(line);
    return 0;
}

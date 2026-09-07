#define _POSIX_C_SOURCE 200809L //must be first - god complex things
#define MAX_JOBS 32
#define JOB_COMMAND_SIZE 128
#define MAX_ARGUMENTS 64
#define MAX_PIPE_COMMANDS 20
#define OUTPUT_FILE_MODE 0644

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>

extern char **environ;

//background jobs

struct Job {
    int job_id;
    pid_t pid;
    char command[JOB_COMMAND_SIZE];
    int is_running;
};
struct Job jobs[MAX_JOBS];
int job_count = 0;
int next_job_id = 1;

int add_job(pid_t pid, const char *command) {
    int slot = -1;
    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].is_running) {
            slot = i;
            break;
        }
    }

    if (slot == -1 && job_count < MAX_JOBS) {
        slot = job_count++;
    }

    if (slot == -1) {
        fprintf(stderr, "abyss-shell: job table full\n");
        return -1;
    }
    jobs[slot].job_id = next_job_id++;
    jobs[slot].pid = pid;

    snprintf(jobs[slot].command, sizeof(jobs[slot].command), "%s", command);
    jobs[slot].is_running = 1;
    return jobs[slot].job_id;
}

void update_jobs(void) {
    int status;
    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].is_running){
            continue;
        }
        pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
        if (result == jobs[i].pid) {
            jobs[i].is_running = 0;
            printf("[%d]+ Done %s\n", jobs[i].job_id, jobs[i].command);
        } else if (result < 0 && errno == ECHILD) {
            jobs[i].is_running = 0;
        }
    }
}

void print_jobs(void) {
    update_jobs();
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].is_running) {
            printf("[%d]+ Running %s\n", jobs[i].job_id, jobs[i].command);
        }
    }
}

int find_job_index(int job_id) {
    for(int i = 0; i < job_count; i++) {
        if(jobs[i].job_id == job_id) {
            return i;
        }
    }
    return -1;
}

int parse_job_id(const char *text) {
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || value <= 0 ||
        value > INT_MAX) {
        return -1;
    }
    return (int)value;
}

void handle_signal(int sig){
    (void)sig; //silence unused parameter handling
    const char msg[] = "\n[abyss-shell] Interrupt Ignore. Hit enter for prompt.\n";
    ssize_t bytes_written = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)bytes_written;
}

void init_signals(void){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; //restart syscalls if interrupted
    if (sigaction(SIGINT, &sa, NULL) != 0){
        perror("sigaction(SIGINT)");
        exit(EXIT_FAILURE);
    }
}


int handle_redirection(char **args, int *arg_count) {
    for (int i = 0; i < *arg_count; i++) {
        int flags;
        if (strcmp(args[i], ">") == 0) {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (strcmp(args[i], ">>") == 0) {
            flags = O_WRONLY | O_CREAT | O_APPEND;
        } else {
            continue;
        }

        if (args[i + 1] == NULL) {
            fprintf(stderr, "abyss-shell: syntax error near unexpected token 'newline'\n");
            return -1;
        }

        int fd = open(args[i + 1], flags, OUTPUT_FILE_MODE);
        if (fd < 0) {
            perror("open");
            return -1;
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd); //stdout now points at the file, this fd copy is no longer needed

        args[i] = NULL;   //truncate argv here so execvp doesn't see "> file.txt" as args
        *arg_count = i;
        return 0;
    }
    return 0; //no redirection operator present, nothing to do
}

void reset_child_signal_handling(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction(SIGINT)");
        _exit(EXIT_FAILURE);
    }
}

void wait_for_child(pid_t pid) {
    while (waitpid(pid, NULL, 0) < 0) {
        if (errno != EINTR) {
            perror("abyss-shell: waitpid");
            break;
        }
    }
}

int main() {
    init_signals();

    while(1) {
        update_jobs();
        char *input = readline("abyss-shell> ");
        if (input == NULL) {
            printf("\n");
            break;
        }

        if (strlen(input) > 0) {
            add_history(input);
        }

        char command_copy[JOB_COMMAND_SIZE];
        snprintf(command_copy, sizeof(command_copy), "%s", input);

        int background = 0;
        size_t input_length = strlen(input);
        while (input_length > 0 && (input[input_length - 1] == ' ' ||
                                    input[input_length - 1] == '\t')) {
            input[--input_length] = '\0';
        }
        if (input_length > 0 && input[input_length - 1] == '&') {
            background = 1;
            input[--input_length] = '\0';
            while (input_length > 0 && (input[input_length - 1] == ' ' ||
                                        input[input_length - 1] == '\t')) {
                input[--input_length] = '\0';
            }
            snprintf(command_copy, sizeof(command_copy), "%s", input);
        }

        //skip processing if user pressed enter
        if (strlen(input) == 0) {
            goto next_iteration;
        }

        char *cmd_string[MAX_PIPE_COMMANDS + 1];
        int N = 0; //tracks the total number of chunks

        char *token = strtok(input, "|");
        while(token != NULL && N < MAX_PIPE_COMMANDS) {
            cmd_string[N] = token;
            N++;
            token = strtok(NULL, "|");
        } 
        cmd_string[N] = NULL;
        
        if (token != NULL) {
            fprintf(stderr, "abyss-shell: too many pipeline commands\n");
            goto next_iteration;
        }
        if (N == 0) goto next_iteration;
        if (background && N > 1) {
            fprintf(stderr, "abyss-shell: background pipelines are not supported\n");
            goto next_iteration;
        }
        //------------------------------------phase-1, done.
        
        //phase - 2, execution and space split
        
        if (N == 1) {
            char *args[MAX_ARGUMENTS];
            int arg_count = 0;

            //tokenize into an array of strings
            char *arg_token = strtok(cmd_string[0], " ");
            while (arg_token != NULL && arg_count < MAX_ARGUMENTS - 1) {
                args[arg_count++] = arg_token;
                arg_token = strtok(NULL, " ");
            }
            args[arg_count] = NULL;

            if (arg_count == 0) goto next_iteration; //skip if it was just a space

            //handle built-in
            if (strcmp(args[0], "exit") == 0) {
                free(input);
                break;
            }
            if (strcmp(args[0], "cd") == 0) {
                if (args[1] != NULL) {
                    if (chdir(args[1]) != 0) {
                        perror("cd");
                    }
                }
                goto next_iteration;
            }
            if (strcmp(args[0], "jobs") == 0) {
                print_jobs();
                goto next_iteration;
            }

            if (strcmp(args[0], "export") == 0) {
                if(args[1] == NULL) {
                    fprintf(stderr, "abyss-shell: export: missing argument\n");
                    goto next_iteration;
                }

                char *equals = strchr(args[1], '=');

                if (equals == NULL) {
                    fprintf(stderr, "abyss-shell: export: invalid format\n");
                    goto next_iteration;
                }

                *equals = '\0';

                char *name = args[1];
                char *value = equals + 1;

                if(*name == '\0') {
                    fprintf(stderr, "abyss-shell: export invalid variable name\n");
                    goto next_iteration;
                }

                if (setenv(name, value, 1) != 0) {
                    perror("abyss-shell: export");
                }
                goto next_iteration;
            }

            if (strcmp(args[0], "env") == 0) {
                for (char **env = environ; *env != NULL; env++) {
                    printf("%s\n", *env);
                }
                goto next_iteration;
            }

            if (strcmp(args[0], "fg") == 0) {
                if (args[1] == NULL) {
                    fprintf(stderr, "abyss-shell: fg: job id required\n");
                    goto next_iteration;
                }

                int job_id = parse_job_id(args[1]);

                if (job_id < 0) {
                    fprintf(stderr, "abyss-shell: fg: invalid job id\n");
                    goto next_iteration;
                }

                int index = find_job_index(job_id);

                if (index == -1){
                    fprintf(stderr, "abyss-shell: fg: no such jobs\n");
                    goto next_iteration;
                }

                if(!jobs[index].is_running) {
                    fprintf(stderr, "abyss-shell: fg: job already completed\n");
                    goto next_iteration;
                }

                printf("%s\n", jobs[index].command);
                wait_for_child(jobs[index].pid);
                jobs[index].is_running = 0;
                goto next_iteration;
            }

            //rest commands, fork()
            pid_t pid = fork();
            if (pid == 0) {
                //let the foreground command respond normally to Ctrl+C
                reset_child_signal_handling();

                //execute command
                if (handle_redirection(args, &arg_count) < 0) {
                    exit(EXIT_FAILURE);
                }
                execvp(args[0], args);
                perror("Execution failed!!");
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                if (background) {
                    int job_id = add_job(pid, command_copy);
                    if (job_id >= 0) {
                        printf("[%d] %d\n", job_id, pid);
                    } else {
                        wait_for_child(pid);
                    }
                } else {
                    wait_for_child(pid); //wait for foreground child
                }
            } else {
                perror("Fork Failed!!!");
            }
        }

        //if pipe detected(N>1)
        else {
            //allocate 2 * (N-1) file descriptors
            int num_pipes = N - 1;
            int pipefds[2 * num_pipes];
            pid_t child_pids[MAX_PIPE_COMMANDS];
            int child_count = 0;

            //initialize all pipes
            for (int i = 0; i < num_pipes; i++) {
                if (pipe(&pipefds[2 * i]) < 0) {
                    perror("Pipe creation failed!!");
                    exit(EXIT_FAILURE);
                }
            }

            //spawn N children
            for (int i = 0; i < N; i++) {
                pid_t pid = fork();

                if (pid == 0){
                    
                    //reset SIGINT so the child responds normally to ctrl + c
                    reset_child_signal_handling();

                    //wiring input - mtlb if not first command, read from prev pipe
                    if(i > 0) {
                        dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
                    }
                    //wiring output - if not the last command, write to next pipe
                    if (i < N - 1) {
                        dup2(pipefds[2 * i + 1], STDOUT_FILENO);
                    }
                    //close all pipe copies in child space
                    for (int j = 0; j < 2 * num_pipes; j++) {
                        close(pipefds[j]);
                    }
                    //tokenize spaces for this specific chunk
                    char *args[MAX_ARGUMENTS];
                    int arg_count = 0;
                    char *arg_token = strtok(cmd_string[i], " ");
                    while (arg_token != NULL && arg_count < MAX_ARGUMENTS - 1) {
                        args[arg_count++] = arg_token;
                        arg_token = strtok(NULL, " ");
                    }
                    args[arg_count] = NULL;

                    if (arg_count > 0) {
                        if (handle_redirection(args, &arg_count) < 0) {
                            exit(EXIT_FAILURE);
                        }
                        execvp(args[0], args);
                        perror("Pipe Command Failed");
                    }
                    exit(EXIT_FAILURE);
                } else if (pid < 0) {
                    perror("Pipe fork failed!!");
                } else {
                    child_pids[child_count++] = pid;
                }
            }
            //parent process - close karde saare descriptors so children receive EOF signals
            for (int j = 0; j < 2 * num_pipes; j++) {
                close(pipefds[j]);
            }

            //wait for all N children to terminate cleanly
            for (int i = 0; i < child_count; i++) {
                wait_for_child(child_pids[i]);
            }
        }
    next_iteration:
        free(input);
    } 
    clear_history();
    return 0;
}
#define _POSIX_C_SOURCE 200809L //must be first - god complex things
#define MAX_JOBS 32

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

//background jobs

struct Job {
    int job_id;
    pid_t pid;
    char command[128];
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

    strncpy(jobs[slot].command, command, sizeof(jobs[slot].command) - 1);
    jobs[slot].command[sizeof(jobs[slot].command) - 1] = '\0';
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

        int fd = open(args[i + 1], flags, 0644);
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

int main() {
    init_signals();

    while(1) {
        update_jobs();

        //phase - 1, pipe splits
        char input[1024];
        printf("abyss-shell> ");
        fflush(stdout); //to make sure that prompt shows immediately

        //if fgets() returns NULL, it can be due to SIGINT interruption or actual EOF ctrl+d
        errno = 0;
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\n");
                break;
            }
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin); //clear the stream error flag
                printf("\n"); //jump to fresh line
                continue; // restart the prompt loop
            }
            perror("fgets");
            break;
        }
        input[strcspn(input, "\n")] = '\0'; //will remove trailing newline character

        char command_copy[sizeof(input)];
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
            continue;
        }

        char *cmd_string[20];
        int N = 0; //tracks the total number of chunks

        char *token = strtok(input, "|");
        //prevent buffer overflow
        while(token != NULL && N < 20) {
            cmd_string[N] = token;
            N++;
            token = strtok(NULL, "|");
        } 
        cmd_string[N] = NULL;
        
        if (N == 0) continue;   
        if (background && N > 1) {
            fprintf(stderr, "abyss-shell: background pipelines are not supported\n");
            continue;
        }
        //------------------------------------phase-1, done.
        
        //phase - 2, execution and space split
        
        if (N == 1) {
            char *args[64];
            int arg_count = 0;

            //tokenize into an array of strings
            char *arg_token = strtok(cmd_string[0], " ");
            while (arg_token != NULL && arg_count < 63) {
                args[arg_count++] = arg_token;
                arg_token = strtok(NULL, " ");
            }
            args[arg_count] = NULL;

            if (arg_count == 0) continue; //skip if it was just a space

            //handle built-in
            if (strcmp(args[0], "exit") == 0) {
                break;
            }
            if (strcmp(args[0], "cd") == 0) {
                if (args[1] != NULL) {
                    if (chdir(args[1]) != 0) {
                        perror("cd");
                    }
                }
                continue;
            }
            if (strcmp(args[0], "jobs") == 0) {
                print_jobs();
                continue;
            }

            //rest commands, fork()
            pid_t pid = fork();
            if (pid == 0) {
                //let the foreground command respond normally to Ctrl+C
                struct sigaction sa;
                sa.sa_handler = SIG_DFL;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sigaction(SIGINT, &sa, NULL);

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
                        waitpid(pid, NULL, 0);
                    }
                } else {
                    waitpid(pid, NULL, 0); //wait for foreground child
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
                    struct sigaction sa;
                    sa.sa_handler = SIG_DFL;
                    sigemptyset(&sa.sa_mask);
                    sa.sa_flags = 0;
                    sigaction(SIGINT, &sa, NULL);

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
                    char *args[64];
                    int arg_count = 0;
                    char *arg_token = strtok(cmd_string[i], " ");
                    while (arg_token != NULL && arg_count < 63) {
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
                }
            }
            //parent process - close karde saare descriptors so children receive EOF signals
            for (int j = 0; j < 2 * num_pipes; j++) {
                close(pipefds[j]);
            }

            //wait for all N children to terminate cleanly
            for (int i = 0; i < N; i++) {
                wait(NULL);
            }
        }
    } 
    return 0;
}
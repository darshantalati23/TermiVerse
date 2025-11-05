#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// --- Constants ---
#define MAX_LINE 256
#define MAX_ARGS 16
#define MAX_JOBS 32
#define APP_DIR "bin/"

// --- MODIFICATION: Use home directory for the alarm file ---
static char g_alarm_file_path[1024];

// --- Job States ---
enum JobStatus {
    UNDEFINED,
    FOREGROUND,
    BACKGROUND,
    STOPPED
};

// --- Job Structure ---
struct Job {
    pid_t pid;
    pid_t pgid;
    int job_id;
    enum JobStatus status;
    char cmdline[MAX_LINE];
};

// --- Global Variables ---
struct Job jobs[MAX_JOBS];
int next_job_id = 1;
pid_t shell_pgid;
int shell_terminal_fd;
volatile sig_atomic_t g_alarm_flag = 0;

// --- Function Prototypes ---
void eval(char *cmdline);
int parse_line(char *buf, char **argv);
int is_builtin_command(char **argv);
void run_builtin_command(char **argv);
void launch_job(char **argv, int is_background, char *cmdline);
void wait_for_job(pid_t pid);
void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigusr1_handler(int sig); 
void handle_alarm();          
void init_jobs();
int add_job(pid_t pid, pid_t pgid, int status, char *cmdline);
int delete_job(pid_t pid);
struct Job* get_job_by_pid(pid_t pid);
struct Job* get_job_by_id(int job_id);
struct Job* get_foreground_job();
void list_jobs();

// --- Main Function ---
int main() {
    char cmdline[MAX_LINE];

    // --- MODIFICATION: Set the global alarm file path ---
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Error: Could not find HOME directory. Alarm app will fail.\n");
        // Don't exit, but alarms won't work
    } else {
        snprintf(g_alarm_file_path, sizeof(g_alarm_file_path), "%s/termiverse_alarm.txt", home_dir);
    }
    // --- End modification ---

    shell_terminal_fd = STDIN_FILENO;
    if (!isatty(shell_terminal_fd)) {
        fprintf(stderr, "TermiVerse: Not an interactive terminal.\n");
        exit(1);
    }

    init_jobs();

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; 

    sa.sa_handler = sigchld_handler;
    sa.sa_flags |= SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction (SIGCHLD)");
        exit(1);
    }
    
    sa.sa_handler = sigtstp_handler;
    if (sigaction(SIGTSTP, &sa, NULL) < 0) {
        perror("sigaction (SIGTSTP)");
        exit(1);
    }

    sa.sa_handler = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction (SIGUSR1)");
        exit(1);
    }

    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        perror("setpgid");
        exit(1);
    }
    tcsetpgrp(shell_terminal_fd, shell_pgid);

    printf("Welcome to TermiVerse. Type 'quit' to exit.\n");
    while (1) {
        if (g_alarm_flag) {
            handle_alarm();
            g_alarm_flag = 0;
        }

        printf("TermiVerse> ");
        fflush(stdout);

        if (fgets(cmdline, MAX_LINE, stdin) == NULL) {
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            printf("\nExiting TermiVerse.\n");
            break;
        }

        if (strlen(cmdline) > 1) {
            eval(cmdline);
        }
    }

    return 0;
}

// --- eval: Evaluate the command line ---
void eval(char *cmdline) {
    char *argv[MAX_ARGS];
    char buf[MAX_LINE];
    int is_background;
    char original_cmdline[MAX_LINE]; 

    strcpy(buf, cmdline);
    strcpy(original_cmdline, cmdline); 
    is_background = parse_line(buf, argv);

    if (argv[0] == NULL) {
        return; 
    }

    if (is_builtin_command(argv)) {
        run_builtin_command(argv);
    } 
    else if (strcmp(argv[0], "launch") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, "Usage: launch <app_name> [args...]\n");
            return;
        }
        if (strcmp(argv[1], "alarm") == 0) {
            int i = 1;
            while(argv[i] != NULL) i++;
            if (i < MAX_ARGS - 1) {
                static char pid_str[32];
                snprintf(pid_str, sizeof(pid_str), "%d", shell_pgid);
                argv[i] = pid_str;
                argv[i+1] = NULL;
            }
        }
        launch_job(&argv[1], is_background, original_cmdline);
    }
    else if (strcmp(argv[0], "chat") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, "Usage: chat <username>\n");
            return;
        }
        
        char *chat_argv[MAX_ARGS];
        chat_argv[0] = "chat_client"; 
        chat_argv[1] = argv[1];      
        chat_argv[2] = NULL;         
        
        char chat_cmdline[MAX_LINE];
        snprintf(chat_cmdline, sizeof(chat_cmdline), "launch chat_client %s", argv[1]);

        launch_job(chat_argv, 0, chat_cmdline);
    }
    else if (strcmp(argv[0], "calc") == 0) {
        if (argv[3] == NULL) {
            fprintf(stderr, "Usage: calc <num1> <operator> <num2>\n");
            return;
        }

        char *calc_argv[MAX_ARGS];
        calc_argv[0] = "calculator";
        calc_argv[1] = argv[1];
        calc_argv[2] = argv[2];
        calc_argv[3] = argv[3];
        calc_argv[4] = NULL;
        
        char calc_cmdline[MAX_LINE];
        snprintf(calc_cmdline, sizeof(calc_cmdline), "launch calculator %s %s %s", argv[1], argv[2], argv[3]);

        launch_job(calc_argv, 0, calc_cmdline);
    }
    else {
        fprintf(stderr, "TermiVerse: Unknown command: '%s'. Use 'launch', 'chat', 'calc', or other built-ins.\n", argv[0]);
    }
}

// --- parse_line: (Unchanged) ---
int parse_line(char *buf, char **argv) {
    int bg = 0;
    int argc = 0;
    char *p = buf;
    
    enum { WHITESPACE, IN_ARG, IN_QUOTE } state = WHITESPACE;

    buf[strlen(buf) - 1] = ' '; 
    char *bg_char = strrchr(buf, '&');
    if (bg_char) {
        bg = 1;
        *bg_char = ' '; 
    }

    while (*p) {
        switch(state) {
            case WHITESPACE:
                if (*p == ' ' || *p == '\t') {
                    *p++ = '\0';
                } else if (*p == '"') {
                    state = IN_QUOTE;
                    argv[argc++] = ++p; 
                } else {
                    state = IN_ARG;
                    argv[argc++] = p++;
                }
                break;
            case IN_ARG:
                if (*p == ' ' || *p == '\t') {
                    state = WHITESPACE;
                    *p++ = '\0';
                } else {
                    p++;
                }
                break;
            case IN_QUOTE:
                if (*p == '"') {
                    state = WHITESPACE;
                    *p++ = '\0'; 
                } else {
                    p++;
                }
                break;
        }
        if (argc >= MAX_ARGS - 1) break;
    }
    argv[argc] = NULL;
    return bg;
}


// --- is_builtin_command: (Unchanged) ---
int is_builtin_command(char **argv) {
    if (strcmp(argv[0], "quit") == 0) return 1;
    if (strcmp(argv[0], "jobs") == 0) return 1;
    if (strcmp(argv[0], "fg") == 0) return 1;
    if (strcmp(argv[0], "bg") == 0) return 1;
    return 0;
}

// --- run_builtin_command: (Unchanged) ---
void run_builtin_command(char **argv) {
    if (strcmp(argv[0], "quit") == 0) {
        printf("Exiting TermiVerse.\n");
        exit(0);
    }
    if (strcmp(argv[0], "jobs") == 0) {
        list_jobs();
        return;
    }
    if (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0) {
        if (argv[1] == NULL) {
            printf("%s: command requires a job ID (e.g., %s %%1)\n", argv[0], argv[0]);
            return;
        }
        int job_id;
        if (sscanf(argv[1], "%%%d", &job_id) <= 0) {
            printf("%s: invalid job ID\n", argv[0]);
            return;
        }
        struct Job *job = get_job_by_id(job_id);
        if (job == NULL) {
            printf("%s: job not found: %%%d\n", argv[0], job_id);
            return;
        }
        if (kill(-job->pgid, SIGCONT) < 0) {
            perror("kill (SIGCONT)");
        }
        if (strcmp(argv[0], "fg") == 0) {
            job->status = FOREGROUND;
            wait_for_job(job->pid);
        } else {
            printf("[%d] %s (continued)\n", job->job_id, job->cmdline);
            job->status = BACKGROUND;
        }
        return;
    }
}

// --- launch_job: (Unchanged, but I've added the SIGUSR1 reset) ---
void launch_job(char **argv, int is_background, char *cmdline) {
    pid_t pid;
    sigset_t mask, prev_mask;

    char path[MAX_LINE];
    snprintf(path, sizeof(path), "%s%s", APP_DIR, argv[0]);

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    if ((pid = fork()) == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGUSR1, SIG_DFL); // Reset SIGUSR1 for child

        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        if (execv(path, argv) < 0) {
            fprintf(stderr, "TermiVerse: Command not found: %s\n", argv[0]);
            exit(1);
        }
    } else if (pid > 0) {
        setpgid(pid, pid);
        cmdline[strcspn(cmdline, "\n")] = 0;
        add_job(pid, pid, is_background ? BACKGROUND : FOREGROUND, cmdline);
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        if (is_background) {
            printf("[%d] %d %s\n", next_job_id - 1, pid, cmdline);
        } else {
            wait_for_job(pid);
        }
    } else {
        perror("fork");
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
}

// --- wait_for_job: (Unchanged) ---
void wait_for_job(pid_t pid) {
    struct Job *job = get_job_by_pid(pid);
    if (!job) return;
    tcsetpgrp(shell_terminal_fd, job->pgid);
    while (job->status == FOREGROUND) {
        pause();
    }
    tcsetpgrp(shell_terminal_fd, shell_pgid);
}

// --- Signal Handlers ---

// --- SIGUSR1 handler for alarms ---
void sigusr1_handler(int sig) {
    g_alarm_flag = 1; 
}

// --- Function to process the alarm ---
void handle_alarm() {
    int fd;
    char buffer[MAX_LINE + 100];
    ssize_t bytes_read;

    // --- MODIFICATION: Read from the home directory ---
    fd = open(g_alarm_file_path, O_RDONLY);
    if (fd == -1) {
        // This can happen if the alarm app failed to write the file
        write(STDOUT_FILENO, "\n\n--- ALARM ---\n", 16);
        write(STDOUT_FILENO, "(Error: Could not read alarm message.)", 38);
        write(STDOUT_FILENO, "\n---------------\nTermiVerse> ", 30);
        return;
    }
    
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    unlink(g_alarm_file_path); // Delete the temp file

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        write(STDOUT_FILENO, "\n\n--- ALARM ---\n", 16);
        write(STDOUT_FILENO, buffer, strlen(buffer));
        write(STDOUT_FILENO, "\n---------------\nTermiVerse> ", 30);
    } else {
        // This handles the `launch alarm 5 ""` case
        write(STDOUT_FILENO, "\n\n--- ALARM ---\n", 16);
        write(STDOUT_FILENO, "(Alarm with no message)", 23);
        write(STDOUT_FILENO, "\n---------------\nTermiVerse> ", 30);
    }
}


// --- sigchld_handler: (Unchanged) ---
void sigchld_handler(int sig) {
    int old_errno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            struct Job *job = get_job_by_pid(pid);
            if (job) {
                if (job->status == BACKGROUND) {
                    printf("\n[%d] Done %s\nTermiVerse> ", job->job_id, job->cmdline);
                    fflush(stdout);
                }
                delete_job(pid);
            }
        } else if (WIFSTOPPED(status)) {
            struct Job *job = get_job_by_pid(pid);
            if (job) {
                job->status = STOPPED;
                printf("\n[%d] Stopped %s\nTermiVerse> ", job->job_id, job->cmdline);
                fflush(stdout);
            }
        }
    }
    errno = old_errno;
}

// --- sigtstp_handler: (Unchanged) ---
void sigtstp_handler(int sig) {
    int old_errno = errno;
    struct Job *fg_job = get_foreground_job();
    if (fg_job) {
        kill(-fg_job->pgid, SIGSTOP);
    }
    errno = old_errno;
}

// --- Job List Helper Functions (All Unchanged) ---
void init_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].pid = 0;
        jobs[i].pgid = 0;
        jobs[i].job_id = 0;
        jobs[i].status = UNDEFINED;
        jobs[i].cmdline[0] = '\0';
    }
}
int add_job(pid_t pid, pid_t pgid, int status, char *cmdline) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].pgid = pgid;
            jobs[i].status = status;
            jobs[i].job_id = next_job_id++;
            strcpy(jobs[i].cmdline, cmdline);
            return 1;
        }
    }
    printf("TermiVerse: Job list is full!\n");
    return 0;
}
int delete_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].pid = 0;
            jobs[i].pgid = 0;
            jobs[i].job_id = 0;
            jobs[i].status = UNDEFINED;
            jobs[i].cmdline[0] = '\0';
            return 1;
        }
    }
    return 0;
}
struct Job* get_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}
struct Job* get_job_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].job_id == job_id) {
            return &jobs[i];
        }
    }
    return NULL;
}
struct Job* get_foreground_job() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].status == FOREGROUND) {
            return &jobs[i];
        }
    }
    return NULL;
}
void list_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] %d ", jobs[i].job_id, jobs[i].pid);
            switch (jobs[i].status) {
                case BACKGROUND: printf("Running "); break;
                case STOPPED:    printf("Stopped "); break;
                default:         printf("Unknown ");
            }
            printf("%s\n", jobs[i].cmdline);
        }
    }
}
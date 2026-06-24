#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// --- UI Color Definitions ---
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[1;36m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RED     "\033[1;31m"
#define COLOR_BLUE    "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_GRAY    "\033[1;30m"

// --- Constants ---
#define MAX_LINE 256
#define MAX_ARGS 16
#define MAX_JOBS 32
#define APP_DIR "bin/"

static char g_alarm_file_path[1024];

// --- Job States ---
enum JobStatus { UNDEFINED, FOREGROUND, BACKGROUND, STOPPED };

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

// --- UI Function Prototypes ---
void print_banner();
void loading_animation();
void print_help();

// --- Main Function ---
int main() {
    char cmdline[MAX_LINE];

    // Setup Alarm Path
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, COLOR_RED "Error: HOME directory not found.\n" COLOR_RESET);
    } else {
        snprintf(g_alarm_file_path, sizeof(g_alarm_file_path), "%s/termiverse_alarm.txt", home_dir);
    }

    // Check Terminal
    shell_terminal_fd = STDIN_FILENO;
    if (!isatty(shell_terminal_fd)) {
        fprintf(stderr, "TermiVerse: Interactive terminal required.\n");
        exit(1);
    }

    init_jobs();

    // Ignore Interactive Signals
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    // Setup Signal Handlers
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    
    // SIGCHLD (Restartable)
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sa.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);
    
    // SIGTSTP (Restartable)
    sa.sa_handler = sigtstp_handler;
    sa.sa_flags = SA_RESTART; 
    sigaction(SIGTSTP, &sa, NULL);

    // SIGUSR1 (NON-RESTARTABLE for Alarm Interrupt)
    sa.sa_flags = 0; 
    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);

    // Take Control of Terminal
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        perror("setpgid");
        exit(1);
    }
    tcsetpgrp(shell_terminal_fd, shell_pgid);

    // --- UI STARTUP SEQUENCE ---
    print_banner();
    loading_animation();
    // ---------------------------

    while (1) {
        if (g_alarm_flag) {
            handle_alarm();
            g_alarm_flag = 0;
        }

        // Styled Prompt
        printf(COLOR_GREEN "[Admin@TermiVerse]" COLOR_MAGENTA " ~ " COLOR_CYAN "$ " COLOR_RESET);
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

// --- UI Functions ---
void print_banner() {
    printf("\033[H\033[J"); // Clear screen
    printf(COLOR_CYAN);
    printf("  ████████╗███████╗██████╗ ███╗   ███╗██╗██╗   ██╗███████╗██████╗ ███████╗███████╗\n");
    printf("  ╚══██╔══╝██╔════╝██╔══██╗████╗ ████║██║██║   ██║██╔════╝██╔══██╗██╔════╝██╔════╝\n");
    printf("     ██║   █████╗  ██████╔╝██╔████╔██║██║██║   ██║█████╗  ██████╔╝███████╗█████╗  \n");
    printf("     ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║██║╚██╗ ██╔╝██╔══╝  ██╔══██╗╚════██║██╔══╝  \n");
    printf("     ██║   ███████╗██║  ██║██║ ╚═╝ ██║██║ ╚████╔╝ ███████╗██║  ██║███████║███████╗\n");
    printf("     ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝╚═╝  ╚═══╝  ╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝\n");
    printf(COLOR_RESET "\n");
    printf("                          " COLOR_GRAY "v2.0 Stable | System Ready" COLOR_RESET "\n\n");
}

void loading_animation() {
    printf("  Booting Kernel... [");
    for(int i = 0; i <= 20; i++) {
        printf(COLOR_GREEN "▓" COLOR_RESET);
        fflush(stdout);
        usleep(25000); 
    }
    printf("] OK\n");
    
    printf("  Loading Modules.. [");
    for(int i = 0; i <= 20; i++) {
        printf(COLOR_GREEN "▓" COLOR_RESET);
        fflush(stdout);
        usleep(15000); 
    }
    printf("] OK\n\n");
    
    printf("  " COLOR_YELLOW "TIP:" COLOR_RESET " Type " COLOR_BOLD "'help'" COLOR_RESET " to see the list of installed applications.\n");
    printf("  " COLOR_YELLOW "TIP:" COLOR_RESET " Use " COLOR_BOLD "'quit'" COLOR_RESET " to exit the system.\n\n");
    usleep(400000);
}

void print_help() {
    printf("\n" COLOR_BOLD "  === TermiVerse Applications ===" COLOR_RESET "\n");
    printf("  " COLOR_CYAN "%-40s" COLOR_RESET " : %s\n", "chat <name>", "Global Chat Room");
    printf("  " COLOR_CYAN "%-40s" COLOR_RESET " : %s\n", "calc <eqn> OR launch calculator <eqn>", "Calculator (e.g. calc 10 + 5)");
    printf("  " COLOR_CYAN "%-40s" COLOR_RESET " : %s\n", "launch alarm <sec> <msg>", "Set Timer (e.g. alarm 5 \"Run\")");
    printf("  " COLOR_CYAN "%-40s" COLOR_RESET " : %s\n", "launch notes <cmd>", "Notes App (add/read/clear)");
    printf("  " COLOR_CYAN "%-40s" COLOR_RESET " : %s\n", "launch <app>", "Manual Launch (snake, tetris)");
    
    printf("\n" COLOR_BOLD "  === System Commands ===" COLOR_RESET "\n");
    printf("  " COLOR_MAGENTA "%-18s" COLOR_RESET " : %s\n", "jobs", "List background jobs");
    printf("  " COLOR_MAGENTA "%-18s" COLOR_RESET " : %s\n", "fg %<id>", "Resume job in foreground");
    printf("  " COLOR_MAGENTA "%-18s" COLOR_RESET " : %s\n", "bg %<id>", "Resume job in background");
    printf("  " COLOR_MAGENTA "%-18s" COLOR_RESET " : %s\n", "help", "Show this menu");
    printf("  " COLOR_MAGENTA "%-18s" COLOR_RESET " : %s\n", "quit", "Shutdown system");
    printf("\n");
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

    if (argv[0] == NULL) return; 

    if (is_builtin_command(argv)) {
        run_builtin_command(argv);
    } 
    else if (strcmp(argv[0], "launch") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, COLOR_RED "  Usage: launch <app_name> [args...]\n" COLOR_RESET);
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
    // --- Aliases ---
    else if (strcmp(argv[0], "chat") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, COLOR_RED "  Usage: chat <username>\n" COLOR_RESET);
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
            fprintf(stderr, COLOR_RED "  Usage: calc <num1> <operator> <num2>\n" COLOR_RESET);
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
        printf(COLOR_RED "  Command not found: '%s'\n" COLOR_RESET, argv[0]);
        printf("  Type " COLOR_BOLD "'help'" COLOR_RESET " to see available applications.\n");
    }
}

// --- parse_line ---
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
                if (*p == ' ' || *p == '\t') *p++ = '\0';
                else if (*p == '"') { state = IN_QUOTE; argv[argc++] = ++p; }
                else { state = IN_ARG; argv[argc++] = p++; }
                break;
            case IN_ARG:
                if (*p == ' ' || *p == '\t') { state = WHITESPACE; *p++ = '\0'; }
                else p++;
                break;
            case IN_QUOTE:
                if (*p == '"') { state = WHITESPACE; *p++ = '\0'; }
                else p++;
                break;
        }
        if (argc >= MAX_ARGS - 1) break;
    }
    argv[argc] = NULL;
    return bg;
}

// --- Built-ins ---
int is_builtin_command(char **argv) {
    if (strcmp(argv[0], "quit") == 0) return 1;
    if (strcmp(argv[0], "jobs") == 0) return 1;
    if (strcmp(argv[0], "fg") == 0) return 1;
    if (strcmp(argv[0], "bg") == 0) return 1;
    if (strcmp(argv[0], "help") == 0) return 1;
    return 0;
}

void run_builtin_command(char **argv) {
    if (strcmp(argv[0], "quit") == 0) {
        printf(COLOR_RED "  System Shutdown Sequence Initiated...\n" COLOR_RESET);
        exit(0);
    }
    if (strcmp(argv[0], "help") == 0) {
        print_help();
        return;
    }
    if (strcmp(argv[0], "jobs") == 0) {
        list_jobs();
        return;
    }
    if (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0) {
        if (argv[1] == NULL) {
            printf("  Usage: %s %%<job_id>\n", argv[0]);
            return;
        }
        int job_id;
        if (sscanf(argv[1], "%%%d", &job_id) <= 0) {
            printf(COLOR_RED "  Invalid job ID.\n" COLOR_RESET);
            return;
        }
        struct Job *job = get_job_by_id(job_id);
        if (job == NULL) {
            printf(COLOR_RED "  Job %%%d not found.\n" COLOR_RESET, job_id);
            return;
        }
        if (kill(-job->pgid, SIGCONT) < 0) perror("kill (SIGCONT)");
        
        if (strcmp(argv[0], "fg") == 0) {
            job->status = FOREGROUND;
            wait_for_job(job->pid);
        } else {
            printf("  [%d] %s (continued)\n", job->job_id, job->cmdline);
            job->status = BACKGROUND;
        }
        return;
    }
}

// --- launch_job ---
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
        signal(SIGUSR1, SIG_DFL); 
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        if (execv(path, argv) < 0) {
            fprintf(stderr, COLOR_RED "  Error: Executable '%s' not found.\n" COLOR_RESET, argv[0]);
            exit(1);
        }
    } else if (pid > 0) {
        setpgid(pid, pid);
        cmdline[strcspn(cmdline, "\n")] = 0;
        add_job(pid, pid, is_background ? BACKGROUND : FOREGROUND, cmdline);
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        if (is_background) {
            printf("  [%d] %d %s\n", next_job_id - 1, pid, cmdline);
        } else {
            wait_for_job(pid);
        }
    } else {
        perror("fork");
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
}

// --- wait_for_job ---
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
void sigusr1_handler(int sig) {
    g_alarm_flag = 1; 
}

void handle_alarm() {
    int fd;
    char buffer[MAX_LINE + 100];
    ssize_t bytes_read;

    fd = open(g_alarm_file_path, O_RDONLY);
    if (fd == -1) {
        char *err_msg = COLOR_RED "\n\n  [!] ALARM SIGNAL RECEIVED (Msg Error)\n\n" COLOR_RESET;
        write(STDOUT_FILENO, err_msg, strlen(err_msg));
        return;
    }
    
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    unlink(g_alarm_file_path); 

    // --- FIX: Use strlen() to calculate exact sizes for write() ---
    const char *header = COLOR_RED "\n\n  [!] ALARM: ";
    const char *footer = COLOR_RESET "\n\n";
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        write(STDOUT_FILENO, header, strlen(header));
        write(STDOUT_FILENO, buffer, bytes_read); // Use the actual bytes read
        write(STDOUT_FILENO, footer, strlen(footer));
    } else {
        const char *empty_msg = "(No message content)";
        write(STDOUT_FILENO, header, strlen(header));
        write(STDOUT_FILENO, empty_msg, strlen(empty_msg));
        write(STDOUT_FILENO, footer, strlen(footer));
    }
    // Reprint prompt
    const char *prompt = COLOR_GREEN "[Admin@TermiVerse]" COLOR_MAGENTA " ~ " COLOR_CYAN "$ " COLOR_RESET;
    write(STDOUT_FILENO, prompt, strlen(prompt));
}

void sigchld_handler(int sig) {
    int old_errno = errno;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        struct Job *job = get_job_by_pid(pid);
        if (job) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                delete_job(pid);
            } else if (WIFSTOPPED(status)) {
                job->status = STOPPED;
                const char *msg = COLOR_YELLOW "\n  [Job Suspended]\n" COLOR_RESET;
                write(STDOUT_FILENO, msg, strlen(msg));
            }
        }
    }
    errno = old_errno;
}

void sigtstp_handler(int sig) {
    int old_errno = errno;
    struct Job *fg_job = get_foreground_job();
    if (fg_job) {
        kill(-fg_job->pgid, SIGSTOP);
    }
    errno = old_errno;
}

// --- Job List Functions ---
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
    for (int i = 0; i < MAX_JOBS; i++) { if (jobs[i].pid == pid) return &jobs[i]; }
    return NULL;
}
struct Job* get_job_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) { if (jobs[i].job_id == job_id) return &jobs[i]; }
    return NULL;
}
struct Job* get_foreground_job() {
    for (int i = 0; i < MAX_JOBS; i++) { if (jobs[i].status == FOREGROUND) return &jobs[i]; }
    return NULL;
}
void list_jobs() {
    printf(COLOR_BOLD "\n  Active Jobs:\n" COLOR_RESET);
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("  [%d] %-8d ", jobs[i].job_id, jobs[i].pid);
            switch (jobs[i].status) {
                case BACKGROUND: printf(COLOR_GREEN "Running" COLOR_RESET); break;
                case STOPPED:    printf(COLOR_RED "Stopped" COLOR_RESET); break;
                default:         printf("Unknown");
            }
            printf("  %s\n", jobs[i].cmdline);
            found = 1;
        }
    }
    if (!found) printf("  (No active jobs)\n");
    printf("\n");
}
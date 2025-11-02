/******************************************************************************
 * launcher.c
 *
 * TermiVerse Job-Control Shell
 *
 * This is an advanced shell that provides a job-control layer.
 * It handles:
 * - Launching processes in foreground or background (&).
 * - Asynchronous reaping of terminated children (SIGCHLD) to prevent zombies.
 * - Process Group and Terminal Control (setpgid, tcsetpgrp) for robust job mgmt.
 * - Signal handling for SIGINT (Ctrl+C) and SIGTSTP (Ctrl+Z).
 * - Built-in commands: quit, jobs, fg, bg.
 *
 ******************************************************************************/

// Define for POSIX feature compatibility (e.g., sigaction)
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // Core UNIX functions: fork, exec, getpid, setpgid, tcsetpgrp, isatty, pause
#include <sys/wait.h>   // waitpid() and associated macros (WNOHANG, WUNTRACED, etc.)
#include <signal.h>     // signal(), sigaction(), kill(), sigemptyset(), etc.
#include <fcntl.h>      // File control (used implicitly by terminal control)
#include <errno.h>      // errno global variable

// --- Constants ---
#define MAX_LINE 256    // Max command line length
#define MAX_ARGS 16     // Max arguments per command
#define MAX_JOBS 32     // Max concurrent jobs
#define APP_DIR "bin/"  // Directory for app executables

// --- Job States ---
enum JobStatus {
    UNDEFINED,
    FOREGROUND,
    BACKGROUND,
    STOPPED
};

// --- Job Structure ---
struct Job {
    pid_t pid;                 // Process ID
    pid_t pgid;                // Process Group ID
    int job_id;                // Job ID (1, 2, 3...)
    enum JobStatus status;     // FG, BG, STOPPED
    char cmdline[MAX_LINE];    // The command that was run
};

// --- Global Variables ---
struct Job jobs[MAX_JOBS];     // The job list
int next_job_id = 1;           // Counter for next job ID
pid_t shell_pgid;              // The shell's own process group ID
int shell_terminal_fd;         // File descriptor for the terminal

// --- Function Prototypes ---
void eval(char *cmdline);
int parse_line(char *buf, char **argv);
int is_builtin_command(char **argv);
void run_builtin_command(char **argv);
void launch_job(char **argv, int is_background, char *cmdline);
void wait_for_job(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);

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

    // --- Initialization ---
    // Make sure we are in an interactive terminal
    shell_terminal_fd = STDIN_FILENO;
    if (!isatty(shell_terminal_fd)) {
        fprintf(stderr, "TermiVerse: Not an interactive terminal.\n");
        exit(1);
    }

    // Initialize the job list
    init_jobs();

    // --- Signal Handling Setup ---
    // We want the shell to ignore these signals. Only child jobs will handle them.
    signal(SIGINT, SIG_IGN);   // Ignore Ctrl+C
    signal(SIGTSTP, SIG_IGN);  // Ignore Ctrl+Z
    signal(SIGTTIN, SIG_IGN);  // Ignore background process read
    signal(SIGTTOU, SIG_IGN);  // Ignore background process write

    // Install the SIGCHLD handler to reap terminated/stopped children
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // We use WUNTRACED in waitpid
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        exit(1);
    }
    
    // This is the handler for our OWN Ctrl+Z (to stop a foreground job)
    // We can't ignore it; we must catch it.
    signal(SIGTSTP, sigtstp_handler);

    // --- Take Control of the Terminal ---
    // Put the shell in its own new process group.
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        perror("setpgid");
        exit(1);
    }
    // Grab control of the terminal
    tcsetpgrp(shell_terminal_fd, shell_pgid);

    // --- Core Shell Loop ---
    printf("Welcome to TermiVerse. Type 'quit' to exit.\n");
    while (1) {
        printf("TermiVerse> ");
        fflush(stdout);

        if (fgets(cmdline, MAX_LINE, stdin) == NULL) {
            // Handle Ctrl+D (EOF)
            printf("\nExiting TermiVerse.\n");
            break;
        }

        if (strlen(cmdline) > 1) { // Ignore empty newlines
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
    char original_cmdline[MAX_LINE]; // Store the original line for the job list

    strcpy(buf, cmdline);
    strcpy(original_cmdline, cmdline); // Save a copy
    is_background = parse_line(buf, argv);

    if (argv[0] == NULL) {
        return; // Empty line
    }

    if (is_builtin_command(argv)) {
        run_builtin_command(argv);
    } 
    else if (strcmp(argv[0], "launch") == 0) { // <-- THIS IS THE FIX
        if (argv[1] == NULL) {
            fprintf(stderr, "Usage: launch <app_name> [args...]\n");
            return;
        }
        // Pass the *app's* name (&argv[1]) and the *original command*
        launch_job(&argv[1], is_background, original_cmdline);
    } 
    else {
        fprintf(stderr, "TermiVerse: Unknown command: '%s'. Use 'launch' or built-ins.\n", argv[0]);
    }
}
// --- parse_line: Parse command line into argv array (State-Machine Version) ---
int parse_line(char *buf, char **argv) {
    int bg = 0;
    int argc = 0;
    char *p = buf;
    
    // State machine states
    enum { WHITESPACE, IN_ARG, IN_QUOTE } state = WHITESPACE;

    // --- Replace newline with space and find background '&' ---
    buf[strlen(buf) - 1] = ' '; 
    char *bg_char = strrchr(buf, '&');
    if (bg_char) {
        bg = 1;
        *bg_char = ' '; // Remove it
    }

    // --- Parse the command line ---
    while (*p) {
        switch(state) {
            case WHITESPACE:
                if (*p == ' ' || *p == '\t') {
                    // Still in whitespace, null-terminate previous token
                    *p++ = '\0';
                } else if (*p == '"') {
                    // Start of a quoted argument
                    state = IN_QUOTE;
                    argv[argc++] = ++p; // Save start of arg (after quote)
                } else {
                    // Start of a regular argument
                    state = IN_ARG;
                    argv[argc++] = p++;
                }
                break;

            case IN_ARG:
                if (*p == ' ' || *p == '\t') {
                    // End of the argument
                    state = WHITESPACE;
                    *p++ = '\0';
                } else {
                    // Continue in argument
                    p++;
                }
                break;

            case IN_QUOTE:
                if (*p == '"') {
                    // End of the quoted argument
                    state = WHITESPACE;
                    *p++ = '\0'; // Terminate arg at the quote
                } else {
                    // Continue in quote
                    p++;
                }
                break;
        }
        
        // Safety check
        if (argc >= MAX_ARGS - 1) break;
    }

    argv[argc] = NULL; // Null-terminate the list
    return bg;
}

// --- is_builtin_command: Check if it's a shell built-in ---
int is_builtin_command(char **argv) {
    if (strcmp(argv[0], "quit") == 0) return 1;
    if (strcmp(argv[0], "jobs") == 0) return 1;
    if (strcmp(argv[0], "fg") == 0) return 1;
    if (strcmp(argv[0], "bg") == 0) return 1;
    return 0;
}

// --- run_builtin_command: Run the built-in ---
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
        // --- Handle fg and bg ---
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

        // Send SIGCONT to the entire process group
        if (kill(-job->pgid, SIGCONT) < 0) {
            perror("kill (SIGCONT)");
        }

        if (strcmp(argv[0], "fg") == 0) {
            // Move job to foreground
            job->status = FOREGROUND;
            wait_for_job(job->pid);
        } else {
            // Move job to background
            printf("[%d] %s (continued)\n", job->job_id, job->cmdline);
            job->status = BACKGROUND;
        }
        return;
    }
}
// --- launch_job: Fork and exec a new job ---
void launch_job(char **argv, int is_background, char *cmdline) {
    pid_t pid;
    sigset_t mask, prev_mask;

    // Construct the full path
    char path[MAX_LINE];
    snprintf(path, sizeof(path), "%s%s", APP_DIR, argv[0]);

    // Block SIGCHLD while we add the job to the list
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev_mask);

    if ((pid = fork()) == 0) {
        // --- CHILD PROCESS ---

        // Put the child in its own new process group
        setpgid(0, 0);

        // Restore default signal handlers
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        // Unblock SIGCHLD
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        // Run the app
        if (execv(path, argv) < 0) {
            fprintf(stderr, "TermiVerse: Command not found: %s\n", argv[0]);
            exit(1);
        }
    } else if (pid > 0) {
        // --- PARENT PROCESS ---

        // Put the child in its process group (avoids race condition)
        setpgid(pid, pid);

        // Clean up the command line for the job list (remove newline)
        cmdline[strcspn(cmdline, "\n")] = 0;

        // Add the job to our job list
        add_job(pid, pid, is_background ? BACKGROUND : FOREGROUND, cmdline);

        // Unblock SIGCHLD
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        if (is_background) {
            // Background job
            printf("[%d] %d %s\n", next_job_id - 1, pid, cmdline);
        } else {
            // Foreground job: wait for it
            wait_for_job(pid);
        }
    } else {
        // Fork failed
        perror("fork");
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
} // <-- THIS IS THE CORRECTED BRACE

// --- wait_for_job: Wait for a foreground job to finish or stop ---
void wait_for_job(pid_t pid) {
    struct Job *job = get_job_by_pid(pid);
    if (!job) return;

    // Give terminal control to the foreground job
    tcsetpgrp(shell_terminal_fd, job->pgid);

    // Wait for it to change state (stop or terminate)
    // We use pause() to wait for a signal. The SIGCHLD handler
    // will do the actual state change in the job list.
    while (job->status == FOREGROUND) {
        pause();
    }

    // Take terminal control back
    tcsetpgrp(shell_terminal_fd, shell_pgid);
}

// --- Signal Handlers ---

// sigchld_handler: Reaps children and handles state changes
void sigchld_handler(int sig) {
    int old_errno = errno;
    int status;
    pid_t pid;

    // Use WUNTRACED to also catch jobs that have *stopped* (e.g., Ctrl+Z)
    // Use WNOHANG to be non-blocking
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // Job terminated normally or by a signal
            struct Job *job = get_job_by_pid(pid);
            if (job) {
                if (job->status == BACKGROUND) {
                    // Print done message only for background jobs
                    printf("\n[%d] Done %s\nTermiVerse> ", job->job_id, job->cmdline);
                    fflush(stdout);
                }
                delete_job(pid);
            }
        } else if (WIFSTOPPED(status)) {
            // Job was stopped (Ctrl+Z)
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

// sigtstp_handler: Catches Ctrl+Z
void sigtstp_handler(int sig) {
    int old_errno = errno;

    struct Job *fg_job = get_foreground_job();
    if (fg_job) {
        // Send SIGSTOP to the entire foreground process group
        kill(-fg_job->pgid, SIGSTOP);
    }

    errno = old_errno;
}

// --- Job List Helper Functions ---

// init_jobs: Clear the job list
void init_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].pid = 0;
        jobs[i].pgid = 0;
        jobs[i].job_id = 0;
        jobs[i].status = UNDEFINED;
        jobs[i].cmdline[0] = '\0';
    }
}

// add_job: Add a new job to the list
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

// delete_job: Remove a job from the list
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

// get_job_by_pid: Find a job by its PID
struct Job* get_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            return &jobs[i];
        }
    }
    return NULL;
}

// get_job_by_id: Find a job by its Job ID
struct Job* get_job_by_id(int job_id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].job_id == job_id) {
            return &jobs[i];
        }
    }
    return NULL;
}

// get_foreground_job: Find the (at most) one FG job
struct Job* get_foreground_job() {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].status == FOREGROUND) {
            return &jobs[i];
        }
    }
    return NULL;
}

// list_jobs: Print all current jobs
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
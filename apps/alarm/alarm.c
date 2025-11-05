#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

// --- MODIFICATION: Use home directory for the alarm file ---
static char g_alarm_file_path[1024];

// Global variables to store alarm info
static char g_message[1024];
static pid_t g_launcher_pid;

// --- Signal handler for SIGALRM ---
void sigalrm_handler(int sig) {
    int fd;

    // 1. Write the message to the temp file in the home directory
    fd = open(g_alarm_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        // We can't write the file. Just send the signal and hope.
        // The launcher will see the signal but find no file.
        kill(g_launcher_pid, SIGUSR1);
        exit(1);
    }
    
    // Write the message, then close
    write(fd, g_message, strlen(g_message));
    close(fd);

    // 2. Send SIGUSR1 to the launcher process
    kill(g_launcher_pid, SIGUSR1);

    // 3. Child's work is done
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: launch alarm <seconds> \"<message>\" (Internal PID)\n");
        return 1;
    }
    
    // --- MODIFICATION: Set the global alarm file path ---
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Error: Could not find HOME directory.\n");
        return 1;
    }
    snprintf(g_alarm_file_path, sizeof(g_alarm_file_path), "%s/termiverse_alarm.txt", home_dir);
    // --- End modification ---


    int seconds = atoi(argv[1]);
    strncpy(g_message, argv[2], sizeof(g_message) - 1);
    g_launcher_pid = (pid_t)atoi(argv[3]);

    if (seconds <= 0) {
        fprintf(stderr, "Error: Invalid time in seconds.\n");
        return 1;
    }
    
    if (g_launcher_pid <= 1) {
        fprintf(stderr, "Error: Invalid launcher PID.\n");
        return 1;
    }

    // --- The Detach Fork ---
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    
    if (pid > 0) {
        // --- PARENT Process ---
        printf("Alarm set for %d seconds.\n", seconds);
        exit(0);
    }

    // --- CHILD (Detached) Process ---
    signal(SIGALRM, sigalrm_handler);
    alarm(seconds);
    pause();

    return 0; // Should be unreachable
}
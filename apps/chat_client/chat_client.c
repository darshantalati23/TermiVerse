#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#define MAX_MSG_LEN 1024
#define SERVER_FIFO "/tmp/termiverse_server_fifo"

// --- NEW: ANSI Color Definitions ---
#define COLOR_RESET   "\033[0m"
#define COLOR_ME      "\033[1;36m" // Bright Cyan (Your messages)
#define COLOR_OTHER   "\033[1;33m" // Yellow (Others' messages)
#define COLOR_DM      "\033[1;35m" // Magenta (All DMs)
#define COLOR_SERVER  "\033[1;32m" // Green (Server info)
#define COLOR_ERROR   "\033[1;31m" // Red

// --- MODIFIED Client Data Structure ---
typedef struct {
    int id;
    char username[64];
    char read_fifo[128];  // Server -> Client
    char write_fifo[128]; // Client -> Server
    int read_fd;
    int write_fd;
    int active;
} client_t;

// --- Global variables ---
char g_read_fifo[128];
char g_write_fifo[128];
int g_read_fd;
int g_write_fd;
char g_my_username[64]; // NEW: Store our own username

// --- Function Prototypes ---
void *sender_thread(void *arg);
void cleanup_private_fifos(int sig);
void print_colored_message(const char *buffer); // NEW: Message parser

int main(int argc, char *argv[]) {
    int server_fd;
    client_t login_request;
    char buffer[MAX_MSG_LEN];

    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Usage: chat <username>\n" COLOR_RESET);
        return 1;
    }
    
    // --- NEW: Store our username globally ---
    strncpy(g_my_username, argv[1], sizeof(g_my_username) - 1);
    g_my_username[sizeof(g_my_username) - 1] = '\0';
    
    strncpy(login_request.username, g_my_username, sizeof(login_request.username) - 1);
    login_request.username[sizeof(login_request.username) - 1] = '\0';

    // --- 1. Create our TWO private FIFOs ---
    int pid = getpid();
    snprintf(g_read_fifo, sizeof(g_read_fifo), "/tmp/client_%d_R", pid);
    snprintf(g_write_fifo, sizeof(g_write_fifo), "/tmp/client_%d_W", pid);
    
    strcpy(login_request.read_fifo, g_read_fifo);
    strcpy(login_request.write_fifo, g_write_fifo);

    signal(SIGINT, cleanup_private_fifos);
    signal(SIGTERM, cleanup_private_fifos);

    umask(0);
    if (mkfifo(g_read_fifo, 0666) == -1 || mkfifo(g_write_fifo, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo(private)");
            cleanup_private_fifos(0);
            return 1;
        }
    }

    // --- 2. Send Login Request to Server ---
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd == -1) {
        perror("open(server_fd)");
        fprintf(stderr, COLOR_ERROR "Is the chat server running?\n" COLOR_RESET);
        cleanup_private_fifos(0);
        return 1;
    }

    if (write(server_fd, &login_request, sizeof(client_t)) != sizeof(client_t)) {
        fprintf(stderr, COLOR_ERROR "Error: Failed to send login request to server.\n" COLOR_RESET);
        close(server_fd);
        cleanup_private_fifos(0);
        return 1;
    }
    close(server_fd);

    // --- 3. Open our private FIFOs ---
    printf("Connecting to chat... (PID: %d)\n", pid);

    g_read_fd = open(g_read_fifo, O_RDONLY);
    if (g_read_fd == -1) {
        perror("open(g_read_fd)");
        cleanup_private_fifos(0);
        return 1;
    }

    g_write_fd = open(g_write_fifo, O_WRONLY);
    if (g_write_fd == -1) {
        perror("open(g_write_fd)");
        close(g_read_fd);
        cleanup_private_fifos(0);
        return 1;
    }

    printf(COLOR_SERVER "Connected! Type '/quit' to exit. Type '/list' for users.\n" COLOR_RESET);
    printf("> "); // NEW: Print initial prompt
    fflush(stdout);

    // --- 4. Create a separate thread for sending messages ---
    pthread_t tid;
    if (pthread_create(&tid, NULL, sender_thread, NULL) != 0) {
        fprintf(stderr, COLOR_ERROR "Error: Failed to create sender thread.\n" COLOR_RESET);
        close(g_read_fd);
        close(g_write_fd);
        cleanup_private_fifos(0);
        return 1;
    }
    pthread_detach(tid);

    // --- 5. Main thread loop: Receive and print messages ---
    ssize_t bytes_read;
    while ((bytes_read = read(g_read_fd, buffer, MAX_MSG_LEN - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        // --- MODIFICATION: Use the color parser ---
        printf("\r"); // Move cursor to start of line
        print_colored_message(buffer);
        printf("> "); // Reprint prompt
        fflush(stdout);
    }

    printf("\nDisconnected from server.\n");
    close(g_read_fd);
    close(g_write_fd);
    cleanup_private_fifos(0);

    return 0;
}

// --- NEW: Function to parse and print colored messages ---
void print_colored_message(const char *buffer) {
    // Check for Server messages
    if (strncmp(buffer, "[SERVER]", 8) == 0) {
        printf("%s%s%s\n", COLOR_SERVER, buffer, COLOR_RESET);
    }
    // Check for all DMs (incoming and outgoing)
    else if (strstr(buffer, "(DM)") != NULL) {
        printf("%s%s%s\n", COLOR_DM, buffer, COLOR_RESET);
    }
    // Check for *my* public message (which the server now broadcasts back)
    // We *would* do this, but the server filters it. We'll print our own.
    
    // Default: Must be a public message from *another* user
    else {
        printf("%s%s%s\n", COLOR_OTHER, buffer, COLOR_RESET);
    }
}


// --- sender_thread: Reads user input and sends it to the server ---
void *sender_thread(void *arg) {
    char buffer[MAX_MSG_LEN];

    while (1) {
        // Read from standard input
        if (fgets(buffer, MAX_MSG_LEN, stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline

            if (strcmp(buffer, "/quit") == 0) {
                kill(getpid(), SIGINT); // Trigger cleanup
                break;
            }

            // --- MODIFICATION: Print our own message locally ---
            if (buffer[0] != '/') {
                // \033[A: Move cursor up one line
                // \033[K: Clear the line
                // This replaces "> my message" with "[You] my message"
                printf("\033[A\033[K"); 
                printf("%s[You] %s%s\n", COLOR_ME, buffer, COLOR_RESET);
            }
            
            // Send the message to the server
            if (write(g_write_fd, buffer, strlen(buffer)) == -1) {
                perror("sender_thread: write");
                break;
            }
            
            // Reprint the prompt for the next input
            if (buffer[0] == '/') {
                // If it was a command, just reprint the prompt
                printf("> ");
                fflush(stdout);
            }
        }
    }
    return NULL;
}

// --- cleanup_private_fifos: Signal handler to remove our FIFOs on exit ---
void cleanup_private_fifos(int sig) {
    unlink(g_read_fifo);
    unlink(g_write_fifo);
    // Print reset code to fix terminal color on exit
    printf(COLOR_RESET "\n"); 
    if (sig != 0) { 
        exit(0);
    }
}
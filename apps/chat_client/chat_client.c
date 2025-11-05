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

// --- MODIFIED Client Data Structure ---
// Must match the server's new struct
typedef struct {
    int id;
    char username[64];
    char read_fifo[128];  // Server -> Client
    char write_fifo[128]; // Client -> Server
    int read_fd;
    int write_fd;
    int active;
} client_t;

// --- MODIFICATION: Global variables for *both* private FIFOs ---
char g_read_fifo[128];
char g_write_fifo[128];
int g_read_fd;
int g_write_fd;

// Function Prototypes
void *sender_thread(void *arg);
void cleanup_private_fifos(int sig);

int main(int argc, char *argv[]) {
    int server_fd;
    client_t login_request;
    char buffer[MAX_MSG_LEN];

    if (argc != 2) {
        fprintf(stderr, "Usage: launch chat_client <username>\n");
        return 1;
    }
    strncpy(login_request.username, argv[1], sizeof(login_request.username) - 1);
    login_request.username[sizeof(login_request.username) - 1] = '\0';

    // --- 1. Create our TWO private FIFOs ---
    int pid = getpid();
    snprintf(g_read_fifo, sizeof(g_read_fifo), "/tmp/client_%d_R", pid);
    snprintf(g_write_fifo, sizeof(g_write_fifo), "/tmp/client_%d_W", pid);
    
    // Copy paths into the login request
    strcpy(login_request.read_fifo, g_read_fifo);
    strcpy(login_request.write_fifo, g_write_fifo);

    signal(SIGINT, cleanup_private_fifos);
    signal(SIGTERM, cleanup_private_fifos);

    umask(0);
    // Create both FIFOs
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
        fprintf(stderr, "Is the chat server running?\n");
        cleanup_private_fifos(0);
        return 1;
    }

    if (write(server_fd, &login_request, sizeof(client_t)) != sizeof(client_t)) {
        fprintf(stderr, "Error: Failed to send login request to server.\n");
        close(server_fd);
        cleanup_private_fifos(0);
        return 1;
    }
    close(server_fd);

    // --- 3. Open our private FIFOs ---
    printf("Connecting to chat... (PID: %d)\n", pid);

    // Open READ-FIFO (Server -> Client) for READING
    g_read_fd = open(g_read_fifo, O_RDONLY);
    if (g_read_fd == -1) {
        perror("open(g_read_fd)");
        cleanup_private_fifos(0);
        return 1;
    }

    // Open WRITE-FIFO (Client -> Server) for WRITING
    g_write_fd = open(g_write_fifo, O_WRONLY);
    if (g_write_fd == -1) {
        perror("open(g_write_fd)");
        close(g_read_fd);
        cleanup_private_fifos(0);
        return 1;
    }

    printf("Connected! Type '/quit' to exit. Type '/list' for users.\n");

    // --- 4. Create a separate thread for sending messages ---
    pthread_t tid;
    if (pthread_create(&tid, NULL, sender_thread, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create sender thread.\n");
        close(g_read_fd);
        close(g_write_fd);
        cleanup_private_fifos(0);
        return 1;
    }
    pthread_detach(tid);

    // --- 5. Main thread loop: Receive and print messages ---
    ssize_t bytes_read;
    // Read from the Server -> Client pipe
    while ((bytes_read = read(g_read_fd, buffer, MAX_MSG_LEN - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);
    }

    printf("Disconnected from server.\n");
    close(g_read_fd);
    close(g_write_fd); // Close write FD in case sender thread is stuck
    cleanup_private_fifos(0);

    return 0;
}

// --- sender_thread: Reads user input and sends it to the server ---
void *sender_thread(void *arg) {
    char buffer[MAX_MSG_LEN];

    // We already opened g_write_fd in main, so we just use it.
    while (1) {
        if (fgets(buffer, MAX_MSG_LEN, stdin) != NULL) {
            buffer[strcspn(buffer, "\n")] = 0; // Remove trailing newline

            if (strcmp(buffer, "/quit") == 0) {
                kill(getpid(), SIGINT); // Trigger cleanup
                break;
            }

            // Write the message to our WRITE-FIFO (Client -> Server)
            if (write(g_write_fd, buffer, strlen(buffer)) == -1) {
                perror("sender_thread: write");
                break;
            }
        }
    }
    // No need to close g_write_fd here, main will handle it
    return NULL;
}

// --- cleanup_private_fifos: Signal handler to remove our FIFOs on exit ---
void cleanup_private_fifos(int sig) {
    // Delete both private FIFOs
    unlink(g_read_fifo);
    unlink(g_write_fifo);
    if (sig != 0) { 
        exit(0);
    }
}
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

// Client Data Structure (must match the server's)
typedef struct {
    int id;
    char username[64];
    char client_fifo[128];
    int client_fd;
    int active;
} client_t;

// Global variable for our private FIFO path
char g_private_fifo[128];

// Function Prototypes
void *sender_thread(void *arg);
void cleanup_private_fifo(int sig);

int main(int argc, char *argv[]) {
    int server_fd, private_fd;
    client_t login_request;
    char buffer[MAX_MSG_LEN];

    if (argc != 2) {
        fprintf(stderr, "Usage: launch chat_client <username>\n");
        return 1;
    }
    strncpy(login_request.username, argv[1], sizeof(login_request.username) - 1);
    login_request.username[sizeof(login_request.username) - 1] = '\0';

    // --- 1. Create our private FIFO ---
    // The path is unique using the process ID (PID)
    snprintf(g_private_fifo, sizeof(g_private_fifo), "/tmp/client_fifo_%d", getpid());
    
    // Set up a signal handler to clean up our FIFO on exit
    signal(SIGINT, cleanup_private_fifo);
    signal(SIGTERM, cleanup_private_fifo);

    umask(0);
    if (mkfifo(g_private_fifo, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo(private)");
            return 1;
        }
    }
    strcpy(login_request.client_fifo, g_private_fifo);

    // --- 2. Send Login Request to Server ---
    // Open the well-known server FIFO for writing
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd == -1) {
        perror("open(server_fd)");
        fprintf(stderr, "Is the chat server running?\n");
        cleanup_private_fifo(0);
        return 1;
    }

    // Write our login struct to the server
    if (write(server_fd, &login_request, sizeof(client_t)) != sizeof(client_t)) {
        fprintf(stderr, "Error: Failed to send login request to server.\n");
        close(server_fd);
        cleanup_private_fifo(0);
        return 1;
    }
    close(server_fd); // We are done with the public FIFO

    // --- 3. Open our private FIFO for reading messages from the server ---
    // This call will BLOCK until the server opens it for writing.
    printf("Connecting to chat... (PID: %d)\n", getpid());
    private_fd = open(g_private_fifo, O_RDONLY);
    if (private_fd == -1) {
        perror("open(private_fd)");
        cleanup_private_fifo(0);
        return 1;
    }
    printf("Connected! Type '/quit' to exit.\n");

    // --- 4. Create a separate thread for sending messages ---
    pthread_t tid;
    if (pthread_create(&tid, NULL, sender_thread, NULL) != 0) {
        fprintf(stderr, "Error: Failed to create sender thread.\n");
        close(private_fd);
        cleanup_private_fifo(0);
        return 1;
    }
    pthread_detach(tid);

    // --- 5. Main thread loop: Receive and print messages ---
    ssize_t bytes_read;
    while ((bytes_read = read(private_fd, buffer, MAX_MSG_LEN - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);
    }

    printf("Disconnected from server.\n");
    close(private_fd);
    cleanup_private_fifo(0); // Clean up and exit

    return 0;
}

// --- sender_thread: Reads user input and sends it to the server ---
void *sender_thread(void *arg) {
    int private_fd_write;
    char buffer[MAX_MSG_LEN];

    // Open our own private FIFO for writing. This is how we send messages.
    // The server has already opened this for reading.
    private_fd_write = open(g_private_fifo, O_WRONLY);
    if (private_fd_write == -1) {
        perror("sender_thread: open");
        return NULL;
    }

    while (1) {
        if (fgets(buffer, MAX_MSG_LEN, stdin) != NULL) {
            // Remove trailing newline
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "/quit") == 0) {
                // Exit signal for the main thread
                // Sending SIGINT to ourselves will trigger the cleanup handler
                kill(getpid(), SIGINT);
                break;
            }

            // Write the message to our FIFO, which the server reads
            if (write(private_fd_write, buffer, strlen(buffer)) == -1) {
                perror("sender_thread: write");
                break;
            }
        }
    }
    close(private_fd_write);
    return NULL;
}

// --- cleanup_private_fifo: Signal handler to remove our FIFO on exit ---
void cleanup_private_fifo(int sig) {
    unlink(g_private_fifo);
    if (sig != 0) { // If called by a real signal, exit
        exit(0);
    }
}
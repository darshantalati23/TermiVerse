#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define MAX_CLIENTS 32
#define MAX_MSG_LEN 1024
#define SERVER_FIFO "/tmp/termiverse_server_fifo" // The "well-known" public FIFO

// --- Client Data Structure ---
typedef struct {
    int id;                 // Unique client ID
    char username[64];
    char client_fifo[128];  // Path to the client's *private* FIFO
    int client_fd;          // File descriptor for writing to the client
    int active;             // 0 = inactive, 1 = active
} client_t;

// --- Global Shared Data ---
client_t g_client_list[MAX_CLIENTS];
int g_client_count = 0;
pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Function Prototypes ---
void *handle_client(void *arg);
void broadcast_message(const char *message, int sender_id);
void add_client(client_t *client);
void remove_client(int client_id);
void cleanup_fifos(int sig);

// --- main ---
int main() {
    int server_fd;
    // char buffer[MAX_MSG_LEN]; // <-- FIX: Removed unused variable
    client_t new_client_request;
    
    signal(SIGINT, cleanup_fifos);
    signal(SIGTERM, cleanup_fifos);
    // Ignore SIGPIPE so the server doesn't crash if it writes to a disconnected client
    signal(SIGPIPE, SIG_IGN); 

    printf("Server starting...\n");

    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_client_list[i].active = 0;
    }

    umask(0); 
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo(server)");
            exit(1);
        }
    }

    printf("Waiting for clients at %s\n", SERVER_FIFO);
    server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd == -1) {
        perror("open(server_fd)");
        exit(1);
    }
    
    while (1) {
        if (read(server_fd, &new_client_request, sizeof(client_t)) == sizeof(client_t)) {
            
            client_t *new_client = (client_t*)malloc(sizeof(client_t));
            if (!new_client) {
                fprintf(stderr, "Error: malloc failed\n");
                continue;
            }
            memcpy(new_client, &new_client_request, sizeof(client_t));

            new_client->client_fd = open(new_client->client_fifo, O_WRONLY);
            if (new_client->client_fd == -1) {
                fprintf(stderr, "Error: Could not open client FIFO %s\n", new_client->client_fifo);
                free(new_client);
                continue;
            }

            pthread_mutex_lock(&g_clients_mutex);
            add_client(new_client);
            pthread_mutex_unlock(&g_clients_mutex);
            
            pthread_t tid;
            if (pthread_create(&tid, NULL, handle_client, (void*)new_client) != 0) {
                fprintf(stderr, "Error: pthread_create failed\n");
                close(new_client->client_fd);
                
                // --- FIX: Get the ID *before* freeing ---
                int client_id_to_remove = new_client->id; 
                free(new_client); // Free the memory
                
                pthread_mutex_lock(&g_clients_mutex);
                remove_client(client_id_to_remove); // Use the saved ID
                pthread_mutex_unlock(&g_clients_mutex);
            }
            
            pthread_detach(tid);

        } else {
            close(server_fd);
            server_fd = open(SERVER_FIFO, O_RDONLY);
        }
    }

    return 0;
}

// --- handle_client (This is the worker thread) ---
void *handle_client(void *arg) {
    client_t *client = (client_t*)arg;
    char buffer[MAX_MSG_LEN];
    char message[MAX_MSG_LEN + 128];
    int client_read_fd;

    client_read_fd = open(client->client_fifo, O_RDONLY);
    if (client_read_fd == -1) {
        perror("open(client_read_fd)");
        
        close(client->client_fd);
        pthread_mutex_lock(&g_clients_mutex);
        remove_client(client->id);
        pthread_mutex_unlock(&g_clients_mutex);
        free(client);
        return NULL;
    }

    snprintf(message, sizeof(message), "[SERVER] %s has joined the chat.", client->username);
    printf("Thread %d: %s\n", client->id, message);
    broadcast_message(message, client->id); 

    ssize_t bytes_read;
    while ((bytes_read = read(client_read_fd, buffer, MAX_MSG_LEN)) > 0) {
        buffer[bytes_read] = '\0';
        
        snprintf(message, sizeof(message), "[%s] %s", client->username, buffer);
        
        broadcast_message(message, client->id);
    }

    snprintf(message, sizeof(message), "[SERVER] %s has left the chat.", client->username);
    printf("Thread %d: %s\n", client->id, message);
    broadcast_message(message, client->id);

    close(client_read_fd);
    close(client->client_fd);
    
    pthread_mutex_lock(&g_clients_mutex);
    remove_client(client->id);
    pthread_mutex_unlock(&g_clients_mutex);

    free(client);
    return NULL;
}

// --- broadcast_message: Send a message to all *other* active clients ---
void broadcast_message(const char *message, int sender_id) {
    pthread_mutex_lock(&g_clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active && g_client_list[i].id != sender_id) {
            
            if (write(g_client_list[i].client_fd, message, strlen(message)) == -1) {
                // Don't print perror, as this will happen normally
                // if a client disconnects. SIGPIPE is ignored.
            }
        }
    }

    pthread_mutex_unlock(&g_clients_mutex);
}

// --- add_client: Add a new client to the global list ---
void add_client(client_t *client) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_client_list[i].active) {
            client->id = i;
            client->active = 1;
            g_client_list[i] = *client;
            g_client_count++;
            return;
        }
    }
    
    fprintf(stderr, "Server full. Rejecting client %s\n", client->username);
    write(client->client_fd, "[SERVER] Chat server is full. Try again later.\n", 47);
    close(client->client_fd);
    free(client);
}

// --- remove_client: Remove a client from the global list ---
void remove_client(int client_id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active && g_client_list[i].id == client_id) {
            g_client_list[i].active = 0;
            g_client_count--;
            printf("Removed client %d. Total clients: %d\n", client_id, g_client_count);
            return;
        }
    }
}

// --- cleanup_fifos: Signal handler to remove public FIFO on exit ---
void cleanup_fifos(int sig) {
    printf("\nServer shutting down... removing FIFO.\n");
    unlink(SERVER_FIFO); // Delete the public FIFO file
    exit(0);
}
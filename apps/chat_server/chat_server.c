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
#include <time.h>

#define MAX_CLIENTS 32
#define MAX_MSG_LEN 1024
#define SERVER_FIFO "/tmp/termiverse_server_fifo"
#define CHAT_LOG_FILE "/tmp/termiverse_chat.log"

// --- MODIFIED Client Data Structure ---
// Now holds paths for TWO private FIFOs
typedef struct {
    int id;
    char username[64];
    char read_fifo[128];  // Server -> Client
    char write_fifo[128]; // Client -> Server
    int read_fd;          // FD to write to the client (Server -> Client)
    int write_fd;         // FD to read from the client (Client -> Server)
    int active;
} client_t;

// --- Global Shared Data ---
client_t g_client_list[MAX_CLIENTS];
int g_client_count = 0;
pthread_mutex_t g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Function Prototypes ---
void *handle_client(void *arg);
void broadcast_message(const char *message, int sender_id);
int send_direct_message(const char *message, int sender_id, const char *target_username);
void send_user_list(int client_id);
void add_client(client_t *client);
void remove_client(int client_id);
void cleanup_fifos(int sig);
void log_message(const char *msg);

// --- main ---
int main() {
    int server_fd;
    client_t new_client_request;
    
    signal(SIGINT, cleanup_fifos);
    signal(SIGTERM, cleanup_fifos);
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
    // Open the main FIFO. This blocks until a client connects.
    server_fd = open(SERVER_FIFO, O_RDONLY);
    if (server_fd == -1) {
        perror("open(server_fd)");
        exit(1);
    }
    
    while (1) {
        // Read a new "login" request from the main FIFO
        if (read(server_fd, &new_client_request, sizeof(client_t)) == sizeof(client_t)) {
            
            client_t *new_client = (client_t*)malloc(sizeof(client_t));
            if (!new_client) {
                fprintf(stderr, "Error: malloc failed\n");
                continue;
            }
            memcpy(new_client, &new_client_request, sizeof(client_t));
            
            // 1. Open the Client's READ-FIFO (Server -> Client) for WRITING
            new_client->read_fd = open(new_client->read_fifo, O_WRONLY);
            if (new_client->read_fd == -1) {
                fprintf(stderr, "Error: Could not open client read_fifo %s\n", new_client->read_fifo);
                free(new_client);
                continue;
            }

            // 2. Open the Client's WRITE-FIFO (Client -> Server) for READING
            new_client->write_fd = open(new_client->write_fifo, O_RDONLY);
            if (new_client->write_fd == -1) {
                fprintf(stderr, "Error: Could not open client write_fifo %s\n", new_client->write_fifo);
                close(new_client->read_fd);
                free(new_client);
                continue;
            }

            // Add client to the global list (protected by mutex)
            pthread_mutex_lock(&g_clients_mutex);
            add_client(new_client);
            pthread_mutex_unlock(&g_clients_mutex);
            
            // Spawn a new thread to handle this client
            pthread_t tid;
            if (pthread_create(&tid, NULL, handle_client, (void*)new_client) != 0) {
                fprintf(stderr, "Error: pthread_create failed\n");
                close(new_client->read_fd);
                close(new_client->write_fd);
                
                int client_id_to_remove = new_client->id; 
                free(new_client);
                
                pthread_mutex_lock(&g_clients_mutex);
                remove_client(client_id_to_remove);
                pthread_mutex_unlock(&g_clients_mutex);
            }
            
            pthread_detach(tid);

        } else {
            // Error on main FIFO, reopen it
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
    char message_out[MAX_MSG_LEN + 256];
    
    // This thread *only* reads from the client's WRITE-FIFO
    int client_read_fd = client->write_fd; 

    // Announce the new user to everyone
    snprintf(message_out, sizeof(message_out), "[SERVER] %s has joined the chat.", client->username);
    printf("Thread %d: %s\n", client->id, message_out);
    broadcast_message(message_out, -1); // Send to all clients (including sender)

    // --- Client Message Loop ---
    ssize_t bytes_read;
    // Read from the Client -> Server pipe
    while ((bytes_read = read(client_read_fd, buffer, MAX_MSG_LEN)) > 0) {
        buffer[bytes_read] = '\0';
        
        // --- Command Parser ---
        if (strncmp(buffer, "/dm ", 4) == 0) {
            char target_username[64];
            char dm_text[MAX_MSG_LEN];
            if (sscanf(buffer + 4, "%63s %[^\n]", target_username, dm_text) >= 2) {
                char ts[8];
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                strftime(ts, sizeof(ts), "[%H:%M]", tm_info);
                snprintf(message_out, sizeof(message_out), "%s [%s (DM)] %s", ts, client->username, dm_text);
                if (!send_direct_message(message_out, client->id, target_username)) {
                    snprintf(message_out, sizeof(message_out), "[SERVER] Error: User '%s' not found.", target_username);
                    write(client->read_fd, message_out, strlen(message_out)); // Write to Server->Client pipe
                }
            } else {
                snprintf(message_out, sizeof(message_out), "[SERVER] Usage: /dm <username> <message>");
                write(client->read_fd, message_out, strlen(message_out)); // Write to Server->Client pipe
            }
        } else if (strcmp(buffer, "/list") == 0) {
            send_user_list(client->id);
        } else if (strcmp(buffer, "/history") == 0) {
            int log_fd = open(CHAT_LOG_FILE, O_RDONLY);
            if (log_fd == -1) {
                const char *no_hist = "[SERVER] No message history yet.";
                write(client->read_fd, no_hist, strlen(no_hist));
            } else {
                char hist_buf[32768];
                ssize_t n = read(log_fd, hist_buf, sizeof(hist_buf) - 1);
                close(log_fd);
                if (n <= 0) {
                    const char *no_hist = "[SERVER] No message history yet.";
                    write(client->read_fd, no_hist, strlen(no_hist));
                } else {
                    hist_buf[n] = '\0';
                    int newline_count = 0;
                    int offset = (int)n - 1;
                    while (offset >= 0) {
                        if (hist_buf[offset] == '\n') {
                            newline_count++;
                            if (newline_count == 20) break;
                        }
                        offset--;
                    }
                    if (offset < 0) offset = 0;
                    else offset++;
                    write(client->read_fd, hist_buf + offset, n - offset);
                }
            }
        } else {
            // Broadcast Message
            char ts[8];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            strftime(ts, sizeof(ts), "[%H:%M]", tm_info);
            snprintf(message_out, sizeof(message_out), "%s [%s] %s", ts, client->username, buffer);
            broadcast_message(message_out, client->id);
        }
    }

    // --- Client Disconnected ---
    snprintf(message_out, sizeof(message_out), "[SERVER] %s has left the chat.", client->username);
    printf("Thread %d: %s\n", client->id, message_out);
    broadcast_message(message_out, client->id);

    // Close both FDs for this client
    close(client_read_fd);  // This is client->write_fd
    close(client->read_fd); // This is the Server->Client write FD
    
    pthread_mutex_lock(&g_clients_mutex);
    remove_client(client->id);
    pthread_mutex_unlock(&g_clients_mutex);
    
    free(client);
    return NULL;
}

// --- log_message: Append a message to the persistent chat log ---
void log_message(const char *msg) {
    int fd = open(CHAT_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) return;
    write(fd, msg, strlen(msg));
    write(fd, "\n", 1);
    close(fd);
}

// --- broadcast_message: Send a message to all active clients ---
void broadcast_message(const char *message, int sender_id) {
    log_message(message);
    pthread_mutex_lock(&g_clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        // Send to active clients, and don't send to sender (unless sender_id is -1)
        if (g_client_list[i].active && g_client_list[i].id != sender_id) {
            // Write to the client's READ-FIFO (Server -> Client)
            if (write(g_client_list[i].read_fd, message, strlen(message)) == -1) {
                // This client is probably disconnected
            }
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

// --- send_direct_message: Sends a message to a specific user ---
int send_direct_message(const char *message, int sender_id, const char *target_username) {
    int target_found = 0;
    char confirmation_msg[MAX_MSG_LEN + 256];
    client_t *sender = NULL;
    
    pthread_mutex_lock(&g_clients_mutex);

    // Find sender
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active && g_client_list[i].id == sender_id) {
            sender = &g_client_list[i];
            break;
        }
    }
    if (sender == NULL) { 
        pthread_mutex_unlock(&g_clients_mutex);
        return 0;
    }

    // Find target
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active && strcmp(g_client_list[i].username, target_username) == 0) {
            // Found target. Send to their READ-FIFO.
            write(g_client_list[i].read_fd, message, strlen(message));
            target_found = 1;
            
            // Send confirmation back to sender's READ-FIFO
            const char* msg_body = strchr(message, ']');
            if (msg_body) {
                msg_body += 2; // Skip "] "
                snprintf(confirmation_msg, sizeof(confirmation_msg), "[You (DM to %s)] %s", 
                         target_username, msg_body);
                write(sender->read_fd, confirmation_msg, strlen(confirmation_msg));
            }
            break; 
        }
    }
    pthread_mutex_unlock(&g_clients_mutex);
    
    return target_found;
}

// --- send_user_list: Sends a list of active users to the requesting client ---
void send_user_list(int client_id) {
    char list_header[] = "[SERVER] Users online:\n";
    char user_line[128];
    int target_fd = -1;

    pthread_mutex_lock(&g_clients_mutex);
    
    // Find the requesting client's READ-FIFO FD
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active && g_client_list[i].id == client_id) {
            target_fd = g_client_list[i].read_fd;
            break;
        }
    }

    if (target_fd == -1) {
        pthread_mutex_unlock(&g_clients_mutex);
        return; 
    }

    write(target_fd, list_header, strlen(list_header));

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_client_list[i].active) {
            snprintf(user_line, sizeof(user_line), " - %s %s\n", 
                     g_client_list[i].username, (g_client_list[i].id == client_id) ? "(You)" : "");
            write(target_fd, user_line, strlen(user_line));
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
    write(client->read_fd, "[SERVER] Chat server is full. Try again later.\n", 47);
    close(client->read_fd);
    close(client->write_fd);
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
    unlink(SERVER_FIFO); 
    exit(0);
}
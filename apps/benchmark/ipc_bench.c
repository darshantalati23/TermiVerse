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
#include <time.h>
#include <sys/time.h>

#define MAX_MSG_LEN 1024
#define SERVER_FIFO "/tmp/termiverse_server_fifo"

typedef struct {
    int id;
    char username[64];
    char read_fifo[128];
    char write_fifo[128];
    int read_fd;
    int write_fd;
    int active;
} client_t;

typedef struct {
    client_t c;
    int messages_sent;
    int messages_received;
} bench_client_t;

int NUM_CLIENTS = 10;
int DURATION = 5; // seconds
volatile int g_running = 1;

void *receiver_thread(void *arg) {
    bench_client_t *bc = (bench_client_t *)arg;
    char buffer[MAX_MSG_LEN];
    while(g_running) {
        ssize_t n = read(bc->c.read_fd, buffer, sizeof(buffer)-1);
        if (n > 0) {
            bc->messages_received++;
        }
    }
    return NULL;
}

void *sender_thread(void *arg) {
    bench_client_t *bc = (bench_client_t *)arg;
    char buffer[MAX_MSG_LEN];
    snprintf(buffer, sizeof(buffer), "BENCHMSG"); // Shorter messages pump faster
    int len = strlen(buffer);
    
    while(g_running) {
        if (write(bc->c.write_fd, buffer, len) > 0) {
            bc->messages_sent++;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc > 1) NUM_CLIENTS = atoi(argv[1]);
    if (argc > 2) DURATION = atoi(argv[2]);
    if (NUM_CLIENTS > 20) NUM_CLIENTS = 20; 
    
    bench_client_t clients[20];
    pthread_t r_threads[20];
    pthread_t s_threads[20];
    
    printf("\n\033[1;36m=== TermiVerse IPC Benchmark ===\033[0m\n");
    printf("Clients: %d concurrent connections\n", NUM_CLIENTS);
    printf("Duration: %d seconds\n", DURATION);
    printf("Connecting to Chat Server...\n");
    
    for(int i=0; i<NUM_CLIENTS; i++) {
        clients[i].messages_sent = 0;
        clients[i].messages_received = 0;
        snprintf(clients[i].c.username, 64, "bench_%d", i);
        snprintf(clients[i].c.read_fifo, 128, "/tmp/bench_r_%d", i);
        snprintf(clients[i].c.write_fifo, 128, "/tmp/bench_w_%d", i);
        
        unlink(clients[i].c.read_fifo);
        unlink(clients[i].c.write_fifo);
        mkfifo(clients[i].c.read_fifo, 0666);
        mkfifo(clients[i].c.write_fifo, 0666);
        
        int s_fd = open(SERVER_FIFO, O_WRONLY);
        if (s_fd == -1) {
            printf("\033[1;31mError: Could not connect to SERVER_FIFO. Is chat_server running?\033[0m\n");
            return 1;
        }
        write(s_fd, &clients[i].c, sizeof(client_t));
        close(s_fd);
    }
    
    usleep(500000); // 0.5s for server to open pipes
    
    for(int i=0; i<NUM_CLIENTS; i++) {
        clients[i].c.read_fd = open(clients[i].c.read_fifo, O_RDONLY | O_NONBLOCK);
        clients[i].c.write_fd = open(clients[i].c.write_fifo, O_WRONLY | O_NONBLOCK);
        
        if (clients[i].c.read_fd == -1 || clients[i].c.write_fd == -1) {
            printf("\033[1;31mFailed to open private pipes for client %d\033[0m\n", i);
            return 1;
        }
    }
    
    printf("All clients connected. Blasting IPC messages...\n");
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    for(int i=0; i<NUM_CLIENTS; i++) {
        pthread_create(&r_threads[i], NULL, receiver_thread, &clients[i]);
        pthread_create(&s_threads[i], NULL, sender_thread, &clients[i]);
    }
    
    sleep(DURATION);
    g_running = 0;
    
    for(int i=0; i<NUM_CLIENTS; i++) {
        pthread_cancel(r_threads[i]);
        pthread_cancel(s_threads[i]);
        pthread_join(r_threads[i], NULL);
        pthread_join(s_threads[i], NULL);
    }
    
    gettimeofday(&end, NULL);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    
    int total_sent = 0;
    int total_recv = 0;
    for(int i=0; i<NUM_CLIENTS; i++) {
        total_sent += clients[i].messages_sent;
        total_recv += clients[i].messages_received;
        
        close(clients[i].c.read_fd);
        close(clients[i].c.write_fd);
        unlink(clients[i].c.read_fifo);
        unlink(clients[i].c.write_fifo);
    }
    
    printf("\n\033[1;32m=== BENCHMARK RESULTS ===\033[0m\n");
    printf("Elapsed Time     : %.2f seconds\n", elapsed);
    printf("Total Msgs Sent  : %d\n", total_sent);
    printf("Total Msgs Recv  : %d\n", total_recv);
    printf("\033[1;33mThroughput       : %.2f messages/sec\033[0m\n\n", total_sent / elapsed);
    
    return 0;
}

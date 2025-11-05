#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * This is the "worker" process.
 * It reads a problem from its standard input (which will be a pipe)
 * and writes the result to its standard output (which will also be a pipe).
 */
void run_worker() {
    double num1, num2, result;
    char op;

    // Read the problem from stdin
    if (scanf("%lf %c %lf", &num1, &op, &num2) != 3) {
        // If input is bad, print an error to stdout and exit
        printf("Error: Invalid problem format\n");
        exit(1);
    }

    // Solve the problem
    switch (op) {
        case '+': result = num1 + num2; break;
        case '-': result = num1 - num2; break;
        case 'x': // Use 'x' for multiplication
        case '*': result = num1 * num2; break;
        case '/': 
            if (num2 == 0) {
                printf("Error: Division by zero\n");
                exit(1);
            }
            result = num1 / num2; 
            break;
        default:
            printf("Error: Invalid operator\n");
            exit(1);
    }
    
    // Print the result to stdout
    printf("%lf\n", result);
    exit(0);
}

/*
 * This is the "master" process.
 * It gets input from the user and uses pipes to
 * send the problem to the worker and get the result back.
 */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: launch calculator <num1> <operator> <num2>\n");
        fprintf(stderr, "Example: launch calculator 10 + 5\n");
        return 1;
    }

    pid_t pid;
    int pipe_to_worker[2];    // Parent writes here, Child reads from pipe_to_worker[0]
    int pipe_from_worker[2];  // Child writes here, Parent reads from pipe_from_worker[0]

    // Create the two unnamed pipes
    if (pipe(pipe_to_worker) == -1 || pipe(pipe_from_worker) == -1) {
        perror("pipe");
        return 1;
    }

    // --- Fork the worker process ---
    pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // --- CHILD (Worker) Process ---

        // Close the pipe ends we don't need
        close(pipe_to_worker[1]); // Close write-end of "to worker"
        close(pipe_from_worker[0]); // Close read-end of "from worker"

        // Redirect our stdin to read from the "to worker" pipe
        // dup2(oldfd, newfd) makes newfd a copy of oldfd
        if (dup2(pipe_to_worker[0], STDIN_FILENO) == -1) {
            perror("dup2 stdin");
            exit(1);
        }
        
        // Redirect our stdout to write to the "from worker" pipe
        if (dup2(pipe_from_worker[1], STDOUT_FILENO) == -1) {
            perror("dup2 stdout");
            exit(1);
        }

        // Close the original FDs, as they are now duplicated
        close(pipe_to_worker[0]);
        close(pipe_from_worker[1]);

        // Run the worker code. It now reads from and writes to the pipes
        // automatically because we redirected its stdin/stdout.
        run_worker();
        exit(0); // Should be unreachable

    } else {
        // --- PARENT (Master) Process ---
        char problem[128];
        char result_buffer[128];
        ssize_t bytes_read;

        // Close the pipe ends we don't need
        close(pipe_to_worker[0]); // Close read-end of "to worker"
        close(pipe_from_worker[1]); // Close write-end of "from worker"

        // 1. Format the problem string
        snprintf(problem, sizeof(problem), "%s %s %s\n", argv[1], argv[2], argv[3]);

        // 2. Send the problem to the worker
        if (write(pipe_to_worker[1], problem, strlen(problem)) == -1) {
            perror("write to worker");
        }
        close(pipe_to_worker[1]); // Close our write-end. This sends EOF to the worker's stdin.

        // 3. Read the result from the worker
        bytes_read = read(pipe_from_worker[0], result_buffer, sizeof(result_buffer) - 1);
        
        if (bytes_read > 0) {
            result_buffer[bytes_read] = '\0'; // Null-terminate
            // Print the result, removing the newline
            printf("Calculator Result: %s", result_buffer); 
        } else {
            fprintf(stderr, "Error: No response from worker.\n");
        }

        // 4. Clean up
        close(pipe_from_worker[0]);
        wait(NULL); // Wait for the child to terminate
    }

    return 0;
}
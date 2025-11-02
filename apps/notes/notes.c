#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>      // For open() and file flags (O_RDWR, O_CREAT, etc.)
#include <sys/file.h>   // For flock() (file locking)
#include <errno.h>      // For errno

#define NOTES_FILE "termiverse_notes.txt"
#define MAX_NOTE_LEN 1024
#define READ_BUFFER_SIZE 4096

// Function to print a standard error message
void print_error(const char *action) {
    fprintf(stderr, "notes: Error %s: %s\n", action, strerror(errno));
}

// --- add_note: Appends a note to the file ---
void add_note(const char *note) {
    int fd;
    
    // 1. Open the file
    // O_WRONLY: Write-only
    // O_APPEND: Append to the end, don't overwrite
    // O_CREAT: Create it if it doesn't exist
    // 0644: File permissions (read/write for owner, read for others)
    fd = open(NOTES_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) {
        print_error("opening file");
        return;
    }

    // 2. ACQUIRE FILE LOCK
    // We request an exclusive lock. If another process holds a lock,
    // this call will BLOCK (wait) until the lock is released.
    if (flock(fd, LOCK_EX) == -1) {
        print_error("acquiring lock");
        close(fd);
        return;
    }

    // --- CRITICAL SECTION: We are the only process that can write ---
    
    // 3. Write the note
    // We use unbuffered write().
    if (write(fd, note, strlen(note)) == -1) {
        print_error("writing to file");
    }

    // 4. Write a newline character
    if (write(fd, "\n", 1) == -1) {
        print_error("writing newline");
    }

    // --- END OF CRITICAL SECTION ---

    // 5. RELEASE FILE LOCK
    // This allows other 'notes' processes to continue.
    if (flock(fd, LOCK_UN) == -1) {
        print_error("releasing lock");
    }

    // 6. Close the file
    close(fd);
    printf("notes: Note added.\n");
}

// --- read_notes: Reads and prints all notes ---
void read_notes() {
    int fd;
    char buffer[READ_BUFFER_SIZE];
    ssize_t bytes_read;

    // 1. Open the file
    // O_RDONLY: Read-only
    fd = open(NOTES_FILE, O_RDONLY);
    if (fd == -1) {
        if (errno == ENOENT) { // ENOENT = Error, No Entry (file doesn't exist)
            printf("notes: No notes found.\n");
            return;
        }
        print_error("opening file for reading");
        return;
    }

    // 2. ACQUIRE FILE LOCK (Shared)
    // We use a "shared" lock. This allows multiple "readers"
    // to read the file at the same time, but blocks any "writers"
    // (which request an exclusive lock).
    if (flock(fd, LOCK_SH) == -1) {
        print_error("acquiring shared lock");
        close(fd);
        return;
    }

    // --- CRITICAL SECTION (Read-only) ---
    
    // 3. Read and print the file in chunks
    printf("--- Your Notes ---\n");
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // We write the buffer to standard output (file descriptor 1)
        if (write(STDOUT_FILENO, buffer, bytes_read) == -1) {
            print_error("writing to stdout");
            break;
        }
    }
    if (bytes_read == -1) {
        print_error("reading file");
    }
    printf("--- End of Notes ---\n");

    // --- END OF CRITICAL SECTION ---

    // 4. RELEASE FILE LOCK
    flock(fd, LOCK_UN);

    // 5. Close the file
    close(fd);
}

// --- clear_notes: Clears the notes file ---
void clear_notes() {
    int fd;
    
    // 1. Open the file
    // O_WRONLY: Write-only
    // O_TRUNC: Truncate (empty) the file upon opening
    // O_CREAT: Create it if it doesn't exist
    fd = open(NOTES_FILE, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd == -1) {
        print_error("clearing file");
        return;
    }

    // 2. ACQUIRE FILE LOCK
    // We get an exclusive lock to prevent anyone from reading/writing
    // while we are clearing it.
    if (flock(fd, LOCK_EX) == -1) {
        print_error("acquiring lock");
        close(fd);
        return;
    }
    
    // (The file is now empty, so the critical section is just holding the lock)

    // 3. RELEASE FILE LOCK
    flock(fd, LOCK_UN);

    // 4. Close the file
    close(fd);
    printf("notes: All notes cleared.\n");
}

void print_usage() {
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "  launch notes add \"Your note text\"\n");
    fprintf(stderr, "  launch notes read\n");
    fprintf(stderr, "  launch notes clear\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: 'add' requires text.\n");
            print_usage();
            return 1;
        }
        add_note(argv[2]);
    } else if (strcmp(argv[1], "read") == 0) {
        read_notes();
    } else if (strcmp(argv[1], "clear") == 0) {
        clear_notes();
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n", argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}
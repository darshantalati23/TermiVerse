#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <vector>
#include <chrono>
#include <signal.h>
#include <sys/select.h> // --- NEW: For a better kbhit() ---

using namespace std;

// ANSI Color Codes
#define COLOR_RESET   "\033[0m"
#define COLOR_BLACK   "\033[30m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

// Game dimensions
const int WIDTH = 60;
const int HEIGHT = 30;

// --- MODIFICATION: Store original terminal settings globally ---
struct termios orig_termios;
bool rawModeEnabled = false;

// --- MODIFICATION: Function to restore terminal settings ---
void disableRawMode() {
    if (!rawModeEnabled) return;
    cout << "\033[?1049l"; // Restore main screen buffer
    cout << "\033[?25h";   // Show cursor
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    rawModeEnabled = false;
}

// --- MODIFICATION: Function to enable raw mode ---
void enableRawMode() {
    if (rawModeEnabled) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    
    // Register the exit function to restore settings on *any* exit
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); // Turn off canonical mode and echo
    raw.c_iflag &= ~(IXON);          // Turn off software flow control (Ctrl+S, Ctrl+Q)
    raw.c_cc[VMIN] = 0;              // Read 0 bytes
    raw.c_cc[VTIME] = 0;             // Wait 0 time

    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    cout << "\033[?1049h"; // Use alternate screen buffer
    cout << "\033[?25l";   // Hide cursor
    rawModeEnabled = true;
}

// --- MODIFICATION: Signal handler for suspend/resume ---
void handle_signal(int sig) {
    if (sig == SIGTSTP) {
        // --- Suspend Signal (Ctrl+Z) ---
        disableRawMode(); // Restore terminal *before* stopping
        signal(SIGTSTP, SIG_DFL); // Reset to default handler
        kill(getpid(), SIGTSTP);  // Resend the stop signal to this process
    } 
    else if (sig == SIGCONT) {
        // --- Continue Signal (fg) ---
        signal(SIGTSTP, handle_signal); // Re-register our handler
        enableRawMode(); // Re-enable raw mode
    }
}

enum Direction { STOP = 0, UP, DOWN, LEFT, RIGHT };

// --- MODIFICATION: A better, non-blocking kbhit() using select() ---
int kbhit() {
    struct timeval tv = { 0L, 0L }; // No wait
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); // Watch stdin
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}


struct Node { // snake head
    int x, y;
    Node* next;
    Node(int x, int y) : x(x), y(y), next(nullptr) {}
};

enum FruitType { NORMAL, SLOW };

struct Fruit {
    int x, y;
    FruitType type;
};

class SnakeGame {
private:
    bool gameOver;
    bool gameStarted;
    bool paused;
    Node* head;
    Node* tail;
    vector<Fruit> fruits;
    Direction dir;
    int score;
    int speed;
    vector<pair<int, int>> obstacles;
    chrono::steady_clock::time_point startTime;
    int maxScore;
    string playerName;
    bool enableObstacles;

    void spawnFruit() {
        Fruit newFruit;
        int attempts = 0;
        while (attempts < 100) {
            newFruit.x = rand() % WIDTH;
            newFruit.y = rand() % HEIGHT;
            if (!isObstacle(newFruit.x, newFruit.y) && !isSnakeBody(newFruit.x, newFruit.y)) {
                int chance = rand() % 100;
                newFruit.type = (chance < 15) ? SLOW : NORMAL;
                fruits.push_back(newFruit);
                return;
            }
            attempts++;
        }
    }

    void spawnObstacles() {
        if (!enableObstacles) return;
        obstacles.clear();
        int numObstacles = 8 + rand() % 25;
        for (int i = 0; i < numObstacles; i++) {
            int x, y;
            do {
                x = rand() % WIDTH;
                y = rand() % HEIGHT;
            } while (isSnakeBody(x, y) || isFruit(x, y));
            obstacles.push_back({x, y});
        }
    }

    bool isObstacle(int x, int y) {
        if (!enableObstacles) return false;
        for (auto& obs : obstacles) {
            if (obs.first == x && obs.second == y) return true;
        }
        return false;
    }

    bool isSnakeBody(int x, int y) {
        for (Node* temp = head; temp; temp = temp->next) {
            if (temp->x == x && temp->y == y) return true;
        }
        return false;
    }

    bool isFruit(int x, int y) {
        for (auto& fruit : fruits) {
            if (fruit.x == x && fruit.y == y) return true;
        }
        return false;
    }

    void clearSnake() {
        while (head) {
            Node* temp = head;
            head = head->next;
            delete temp;
        }
        tail = nullptr;
    }

public:
    SnakeGame() : maxScore(0), head(nullptr), tail(nullptr), enableObstacles(true) {
        resetGame();
    }

    ~SnakeGame() {
        clearSnake();
        disableRawMode(); // Ensure we clean up
    }

    void resetGame() {
        gameOver = false;
        gameStarted = false;
        paused = false;
        dir = STOP;
        score = 0;
        speed = 120000;
        clearSnake();
        head = new Node(WIDTH / 2, HEIGHT / 2);
        tail = head;
        tail->next = new Node(head->x - 1, head->y);
        tail = tail->next;
        tail->next = new Node(head->x - 2, head->y);
        tail = tail->next;
        fruits.clear();
        for (int i = 0; i < (rand() % 7) + 2; i++) {
            spawnFruit();
        }
        spawnObstacles();
    }

    void draw() {
        cout << "\033[H"; // Move cursor to home

        cout << COLOR_BOLD COLOR_WHITE;
        for (int i = 0; i < WIDTH + 2; i++) cout << "■";
        cout << COLOR_RESET << endl;

        for (int y = 0; y < HEIGHT; y++) {
            cout << COLOR_BOLD COLOR_WHITE "■" COLOR_RESET; // Left wall
            for (int x = 0; x < WIDTH; x++) {
                if (x == head->x && y == head->y) {
                    cout << COLOR_BOLD COLOR_YELLOW "●" COLOR_RESET;
                } else if (isFruit(x, y)) {
                    for (auto& fruit : fruits) {
                        if (fruit.x == x && fruit.y == y) {
                            if (fruit.type == NORMAL)
                                cout << COLOR_BOLD COLOR_RED "◆" COLOR_RESET;
                            else
                                cout << COLOR_BOLD COLOR_GREEN "◆" COLOR_RESET;
                            break;
                        }
                    }
                } else if (isObstacle(x, y)) {
                    cout << COLOR_BOLD COLOR_WHITE "▒" COLOR_RESET;
                } else {
                    bool isBody = false;
                    for (Node* temp = head->next; temp; temp = temp->next) {
                        if (temp->x == x && temp->y == y) {
                            cout << COLOR_BOLD COLOR_YELLOW "○" COLOR_RESET;
                            isBody = true;
                            break;
                        }
                    }
                    if (!isBody) cout << " ";
                }
            }
            cout << COLOR_BOLD COLOR_WHITE "■" COLOR_RESET; // Right wall
            cout << endl;
        }

        cout << COLOR_BOLD COLOR_WHITE;
        for (int i = 0; i < WIDTH + 2; i++) cout << "■";
        cout << COLOR_RESET << endl;

        // Game info panel
        if (gameStarted) {
            auto now = chrono::steady_clock::now();
            int elapsedTime = chrono::duration_cast<chrono::seconds>(now - startTime).count();
            cout << COLOR_BOLD COLOR_BLUE " Player: " COLOR_RESET << COLOR_CYAN << playerName
                 << COLOR_BOLD COLOR_BLUE " | Score: " COLOR_RESET << COLOR_GREEN << score 
                 << COLOR_BOLD COLOR_BLUE " | Time: " COLOR_RESET << COLOR_CYAN << elapsedTime << "s"
                 << COLOR_BOLD COLOR_BLUE " | Max Score: " COLOR_RESET << COLOR_YELLOW << maxScore << COLOR_RESET << endl;
        } else {
            cout << COLOR_BOLD COLOR_GREEN "\n  WELCOME TO SNAKE GAME!\n" COLOR_RESET;
            cout << COLOR_BOLD "  Use " COLOR_GREEN "W/A/S/D" COLOR_RESET COLOR_BOLD " to move\n"
                 << "  " COLOR_RED "X" COLOR_RESET COLOR_BOLD " to quit | " COLOR_MAGENTA "▒" COLOR_RESET COLOR_BOLD " are obstacles\n"
                 << "  Collect " COLOR_RED "◆" COLOR_RESET COLOR_BOLD " to grow!" COLOR_RESET << endl;
        }
    }

    // --- MODIFICATION: Input now reads a single byte in raw mode ---
    void input() {
        if (!kbhit()) return;

        char key;
        if (read(STDIN_FILENO, &key, 1) != 1) return; // Read 1 char

        switch (key) {
            case 'w': case 'W': 
                if (!gameStarted) {
                    gameStarted = true;
                    startTime = chrono::steady_clock::now();
                }
                if (dir != DOWN) dir = UP; 
                break;
            case 's': case 'S': 
                if (!gameStarted) {
                    gameStarted = true;
                    startTime = chrono::steady_clock::now();
                }
                if (dir != UP) dir = DOWN; 
                break;
            case 'a': case 'A': 
                if (!gameStarted) {
                    gameStarted = true;
                    startTime = chrono::steady_clock::now();
                }
                if (dir != RIGHT) dir = LEFT; 
                break;
            case 'd': case 'D': 
                if (!gameStarted) {
                    gameStarted = true;
                    startTime = chrono::steady_clock::now();
                }
                if (dir != LEFT) dir = RIGHT; 
                break;
            case 'p': case 'P': paused = !paused; break;
            case 'x': case 'X': gameOver = true; break;
        }
    }

    void logic() {
        if (dir == STOP || paused) return;

        int newX = head->x, newY = head->y;
        if (dir == UP) newY--;
        else if (dir == DOWN) newY++;
        else if (dir == LEFT) newX--;
        else if (dir == RIGHT) newX++;

        if (newX < 0 || newX >= WIDTH || newY < 0 || newY >= HEIGHT || isObstacle(newX, newY)) {
            gameOver = true;
            return;
        }

        for (Node* temp = head->next; temp; temp = temp->next) {
            if (temp->x == newX && temp->y == newY) {
                gameOver = true;
                return;
            }
        }

        Node* newHead = new Node(newX, newY);
        newHead->next = head;
        head = newHead;

        bool ateFruit = false;
        for (auto it = fruits.begin(); it != fruits.end(); ) {
            if (it->x == newX && it->y == newY) {
                if (it->type == NORMAL) {
                    score += 10;
                    speed = max(45000, speed - 15000);
                } else {
                    score += 5;
                    speed = min(300000, speed + 10000);
                }
                ateFruit = true;
                it = fruits.erase(it);
            } else {
                ++it;
            }
        }

        if (ateFruit) {
            spawnFruit();
        } else {
            Node* temp = head;
            while (temp->next->next) temp = temp->next;
            delete temp->next;
            temp->next = nullptr;
            tail = temp;
        }
    }

    // --- MODIFICATION: The main run() function now manages terminal state ---
    void run() {
        // --- 1. SETUP (Normal Mode) ---
        // Register signal handlers
        signal(SIGTSTP, handle_signal); // Handle Ctrl+Z
        signal(SIGCONT, handle_signal); // Handle 'fg'

        // --- 2. WELCOME SCREEN (Normal Mode) ---
        cout << "\033[H\033[J"; // Clear screen
        cout << COLOR_BOLD COLOR_GREEN "Welcome to SNAKE!" << endl;
        cout << "----------------------" << COLOR_RESET << endl;
        cout << "Enter your name: ";
        cin >> playerName; // Use normal 'cin'

        char obstacleChoice;
        cout << "Do you want obstacles? (y/n): ";
        cin >> obstacleChoice;
        enableObstacles = (obstacleChoice == 'y' || obstacleChoice == 'Y');
        
        cout << "\nStarting game..." << endl;
        usleep(1000000); // 1 second pause

        // --- 3. GAME LOOP (Restart Loop) ---
        while (true) {
            resetGame();
            
            // --- 4. ENTER GAME MODE ---
            enableRawMode();
            
            while (!gameOver) { // This is the main play loop
                draw();
                input(); // Reads raw keys
                if (!paused) {
                    logic();
                }
                int adjusted_speed = speed;
                if (dir == UP || dir == DOWN) {
                    adjusted_speed = (speed * 3) / 2;
                }
                usleep(adjusted_speed);
            }
            
            // --- 5. EXIT GAME MODE ---
            disableRawMode();
            
            maxScore = max(maxScore, score);
            
            // --- 6. GAME OVER SCREEN (Normal Mode) ---
            cout << "\033[H\033[J"; // Clear screen
            cout << COLOR_BOLD COLOR_RED "\n  GAME OVER!\n\n" COLOR_RESET
                 << COLOR_BOLD "  Final Score: " COLOR_GREEN << score << COLOR_RESET "\n"
                 "  Press " COLOR_RED "X" COLOR_RESET COLOR_BOLD " to exit: " COLOR_RESET;
            
            // --- 7. READ RESTART/EXIT (Normal Mode) ---
            char choice;
            while (true) {
                cin >> choice;
                if (choice == 'r' || choice == 'R') {
                    break; // Will loop back to resetGame() and enableRawMode()
                } else if (choice == 'x' || choice == 'X') {
                    return; // Will exit the run() function.
                } else {
                    cout << "Invalid. Press 'R' or 'X': ";
                }
            }
        }
    }
};

int main() {
    srand(time(0));
    SnakeGame game;
    game.run();
    // atexit(disableRawMode) will be called on return
    return 0;
}
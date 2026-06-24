#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string>
#include <signal.h>
#include <sys/select.h>
#include <libgen.h>         // --- MODIFICATION: For dirname()
#include <limits.h>

using namespace std;

#define WIDTH 10
#define HEIGHT 22
#define BLOCK "\u2588\u2588"
#define GHOST "\u2591\u2591"
#define EMPTY "  "

// ANSI color codes
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_ORANGE  "\x1b[38;5;208m"
#define ANSI_COLOR_WHITE   "\x1b[37m"
#define ANSI_COLOR_GHOST   "\x1b[37;2m"
#define ANSI_COLOR_BOLD    "\x1b[1m"


// --- MODIFICATION: Global variable for our executable's directory ---
string g_exe_dir_path;


// --- Terminal control globals and functions ---
struct termios orig_termios;
bool rawModeEnabled = false;

void disableRawMode() {
    if (!rawModeEnabled) return;
    cout << "\033[?1049l"; 
    cout << "\033[?25h";   
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    rawModeEnabled = false;
}

void enableRawMode() {
    if (rawModeEnabled) return;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    cout << "\033[?1049h"; 
    cout << "\033[?25l";   
    rawModeEnabled = true;
}

void handle_signal(int sig) {
    if (sig == SIGTSTP) {
        disableRawMode();
        signal(SIGTSTP, SIG_DFL);
        kill(getpid(), SIGTSTP);
    } else if (sig == SIGCONT) {
        signal(SIGTSTP, handle_signal);
        enableRawMode();
    }
}

int kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
}


enum class TetrominoType { I, O, T, S, Z, J, L };

class Tetromino {
// (This class is unchanged)
private:
    TetrominoType type;
    int rotation;
    int x, y;
    vector<vector<int>> shape;
    string color;

    void initShape() {
        switch(type) {
            case TetrominoType::I:
                shape = {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}};
                color = ANSI_COLOR_CYAN; break;
            case TetrominoType::O:
                shape = {{1,1}, {1,1}};
                color = ANSI_COLOR_YELLOW; break;
            case TetrominoType::T:
                shape = {{0,1,0}, {1,1,1}, {0,0,0}};
                color = ANSI_COLOR_MAGENTA; break;
            case TetrominoType::S:
                shape = {{0,1,1}, {1,1,0}, {0,0,0}};
                color = ANSI_COLOR_GREEN; break;
            case TetrominoType::Z:
                shape = {{1,1,0}, {0,1,1}, {0,0,0}};
                color = ANSI_COLOR_RED; break;
            case TetrominoType::J:
                shape = {{1,0,0}, {1,1,1}, {0,0,0}};
                color = ANSI_COLOR_BLUE; break;
            case TetrominoType::L:
                shape = {{0,0,1}, {1,1,1}, {0,0,0}};
                color = ANSI_COLOR_ORANGE; break;
        }
    }

public:
    Tetromino(TetrominoType t) : type(t), rotation(0), x(WIDTH/2-2), y(0) { initShape(); }

    void rotate() {
        rotation = (rotation + 1) % 4;
        vector<vector<int>> newShape(shape[0].size(), vector<int>(shape.size()));
        for (size_t i = 0; i < shape.size(); ++i)
            for (size_t j = 0; j < shape[0].size(); ++j)
                newShape[j][shape.size()-1-i] = shape[i][j];
        shape = newShape;
    }

    const vector<vector<int>>& getShape() const { return shape; }
    int getX() const { return x; }
    int getY() const { return y; }
    string getColor() const { return color; }
    void move(int dx, int dy) { x += dx; y += dy; }
    Tetromino* clone() const { return new Tetromino(*this); }
};

class Grid {
private:
    vector<vector<string>> grid;

public:
    Grid() : grid(HEIGHT, vector<string>(WIDTH, "")) {}

    bool isCollision(const Tetromino& t) const {
    // (This function is unchanged)
        for (size_t i = 0; i < t.getShape().size(); ++i) {
            for (size_t j = 0; j < t.getShape()[i].size(); ++j) {
                if (t.getShape()[i][j]) {
                    int nx = t.getX() + j;
                    int ny = t.getY() + i;
                    if (nx < 0 || nx >= WIDTH || ny >= HEIGHT) return true;
                    if (ny >= 0 && grid[ny][nx] != "") return true;
                }
            }
        }
        return false;
    }

    void merge(const Tetromino& t) {
    // (This function is unchanged)
        for (size_t i = 0; i < t.getShape().size(); ++i) {
            for (size_t j = 0; j < t.getShape()[i].size(); ++j) {
                if (t.getShape()[i][j]) {
                    int x = t.getX() + j;
                    int y = t.getY() + i;
                    if (y >= 0) grid[y][x] = t.getColor();
                }
            }
        }
    }

    int clearLines() {
        int lines = 0;
        for (int y = HEIGHT-1; y >= 0; --y) {
            bool full = true;
            for (int x = 0; x < WIDTH; ++x)
                if (grid[y][x] == "") { full = false; break; }

            if (full) {
                grid.erase(grid.begin() + y);
                grid.insert(grid.begin(), vector<string>(WIDTH, ""));
                lines++;
                y++;
            }
        }
        if (lines > 0) {
            // --- MODIFICATION: Use the absolute path ---
            string cmd = "aplay -q " + g_exe_dir_path + "/pop.wav &";
            system(cmd.c_str());
        }
        return lines;
    }

    const vector<vector<string>>& getGrid() const { return grid; }
};

class Game {
// (All private members are unchanged)
private:
    Grid grid;
    Tetromino* current;
    int score;
    int level;
    bool gameOver;
    bool paused;
    string playerName;

    void printInstructions() const {
    // (This function is unchanged)
        cout << "\033[H\033[J"; // Clear screen
        cout << ANSI_COLOR_BOLD << ANSI_COLOR_GREEN << "Welcome to TETRIS!" << ANSI_COLOR_RESET << endl;
        cout << "--------------------" << endl;
        cout << "Player Name: " << playerName << endl << endl;
        cout << ANSI_COLOR_BOLD << "HOW TO PLAY:" << ANSI_COLOR_RESET << endl;
        cout << ANSI_COLOR_YELLOW << "  A" << ANSI_COLOR_RESET << " - Move Left" << endl;
        cout << ANSI_COLOR_YELLOW << "  D" << ANSI_COLOR_RESET << " - Move Right" << endl;
        cout << ANSI_COLOR_YELLOW << "  W" << ANSI_COLOR_RESET << " - Rotate" << endl;
        cout << ANSI_COLOR_YELLOW << "  S" << ANSI_COLOR_RESET << " - Soft Drop" << endl;
        cout << ANSI_COLOR_YELLOW << "  Space" << ANSI_COLOR_RESET << " - Hard Drop" << endl;
        cout << ANSI_COLOR_YELLOW << "  P" << ANSI_COLOR_RESET << " - Pause/Resume" << endl;
        cout << ANSI_COLOR_YELLOW << "  Q" << ANSI_COLOR_RESET << " - Quit" << endl << endl;
        cout << "Press " << ANSI_COLOR_GREEN << "Enter" << ANSI_COLOR_RESET << " to start...";
        
        char c;
        while(read(STDIN_FILENO, &c, 1) == 1 && c != '\n');
    }

    Tetromino* newPiece() {
    // (This function is unchanged)
        TetrominoType types[] = {TetrominoType::I, TetrominoType::O, TetrominoType::T,
                                 TetrominoType::S, TetrominoType::Z, TetrominoType::J, TetrominoType::L};
        return new Tetromino(types[rand() % 7]);
    }

    void drawGhost(Tetromino* ghost, vector<vector<string>>& tempGrid) const {
    // (This function is unchanged)
        while (!grid.isCollision(*ghost)) ghost->move(0, 1);
        ghost->move(0, -1);
        
        for (size_t i = 0; i < ghost->getShape().size(); ++i) {
            for (size_t j = 0; j < ghost->getShape()[i].size(); ++j) {
                if (ghost->getShape()[i][j]) {
                    int x = ghost->getX() + j;
                    int y = ghost->getY() + i;
                    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
                        tempGrid[y][x] = ANSI_COLOR_GHOST;
                    }
                }
            }
        }
    }

    void draw() const {
    // (This function is unchanged)
        cout << "\033[H"; 
        cout << ANSI_COLOR_RESET;
        
        string scoreLine = "Player: " + playerName + " | Score: " + to_string(score) + " | Level: " + to_string(level);
        int padding = ((WIDTH*2 + 4) - scoreLine.length()) / 2;
        cout << string(padding > 0 ? padding : 0, ' ') << scoreLine << "\n\n";

        vector<vector<string>> tempGrid = grid.getGrid();
        
        Tetromino* ghost = current->clone();
        drawGhost(ghost, tempGrid);
        delete ghost;

        const auto& shape = current->getShape();
        int tx = current->getX();
        int ty = current->getY();
        for (size_t i = 0; i < shape.size(); ++i) {
            for (size_t j = 0; j < shape[i].size(); ++j) {
                if (shape[i][j]) {
                    int x = tx + j;
                    int y = ty + i;
                    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
                        tempGrid[y][x] = current->getColor();
                    }
                }
            }
        }

        cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
        for (int x = 0; x < WIDTH; x++) cout << BLOCK;
        cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET << endl;

        for (int y = 0; y < HEIGHT; ++y) {
            cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
            for (int x = 0; x < WIDTH; ++x) {
                if (tempGrid[y][x] != "") {
                    if (tempGrid[y][x] == ANSI_COLOR_GHOST) {
                        cout << ANSI_COLOR_GHOST << GHOST << ANSI_COLOR_RESET;
                    } else {
                        cout << tempGrid[y][x] << BLOCK << ANSI_COLOR_RESET;
                    }
                } else {
                    cout << EMPTY;
                }
            }
            cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET << endl;
        }

        cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
        for (int x = 0; x < WIDTH; x++) cout << BLOCK;
        cout << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET << endl;

        if (paused) {
            string pauseMsg = "PAUSED";
            int pausePad = ((WIDTH*2 + 4) - pauseMsg.length()) / 2;
            cout << string(pausePad > 0 ? pausePad : 0, ' ') << ANSI_COLOR_BOLD << ANSI_COLOR_YELLOW << pauseMsg << ANSI_COLOR_RESET << endl;
        }
    }

    void handleInput() {
    // (This function is unchanged)
        if (!kbhit()) return;

        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) != 1) return;

        if (paused) {
            if (tolower(ch) == 'p') paused = false;
            return;
        }

        Tetromino temp = *current;
        switch(tolower(ch)) {
            case 'a': temp.move(-1, 0); break;
            case 'd': temp.move(1, 0); break;
            case 'w': temp.rotate(); break;
            case 's': temp.move(0, 1); break;
            case ' ': 
                while (!grid.isCollision(temp)) { 
                    current->move(0, 1);
                    temp = *current;
                }
                current->move(0, -1);
                break;
            case 27: case 'q': gameOver = true; break;
            case 'p': paused = true; break;
        }

        if (!grid.isCollision(temp)) *current = temp;
    }

public:
    Game() : score(0), level(1), gameOver(false), paused(false) {
    // (This function is unchanged)
        srand(time(0));
        cout << "\033[H\033[J"; 
        cout << "Enter player name: ";
        getline(cin, playerName);
        printInstructions();
        current = newPiece();
    }

    ~Game() { delete current; }

    void update() {
    // (This function is unchanged)
        if (paused) return;
        Tetromino temp = *current;
        temp.move(0, 1);

        if (grid.isCollision(temp)) {
            grid.merge(*current);
            int lines = grid.clearLines();
            score += lines * 100 * level;
            level += lines / 5;
            delete current;
            current = newPiece();
            if (grid.isCollision(*current)) gameOver = true;
        } else {
            *current = temp;
        }
    }

    void run() {
        signal(SIGTSTP, handle_signal);
        signal(SIGCONT, handle_signal);
        
        enableRawMode();

        while (!gameOver) {
            draw();
            handleInput();
            update();
            usleep(200000 / level);
        }
        
        disableRawMode();
        
        cout << "\033[H\033[J"; 
        cout << ANSI_COLOR_BOLD << ANSI_COLOR_RED << "GAME OVER!" << ANSI_COLOR_RESET << endl;
        cout << "Final Score: " << ANSI_COLOR_GREEN << score << ANSI_COLOR_RESET << endl;
        
        // --- MODIFICATION: Use the absolute path ---
        string cmd = "aplay -q " + g_exe_dir_path + "/pop2.wav &";
        system(cmd.c_str());
    }
};

int main() {
    // --- MODIFICATION: Get the executable's path ---
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        g_exe_dir_path = dirname(exe_path); // Get the directory part
    } else {
        g_exe_dir_path = "."; // Fallback if readlink fails
    }

    Game game;
    game.run();
    return 0;
}
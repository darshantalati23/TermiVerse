#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <string>
#include <cctype>
#include <sstream>
#include <signal.h>       // --- MODIFICATION: Added for signals
#include <sys/select.h>   // --- MODIFICATION: Added for non-blocking read
#include <libgen.h>         // --- MODIFICATION: For dirname()
#include <linux/limits.h>   // --- MODIFICATION: For PATH_MAX

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


// --- MODIFICATION: Terminal control globals and functions ---
struct termios orig_termios;
bool rawModeEnabled = false;

void disableRawMode() {
    if (!rawModeEnabled) return;
    cout << "\033[?1049l"; // Restore main screen buffer
    cout << "\033[?25h";   // Show cursor
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

    cout << "\033[?1049h"; // Use alternate screen buffer
    cout << "\033[?25l";   // Hide cursor
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
// --- END OF MODIFIED TERMINAL CONTROL ---

// Tetromino Class (unchanged)
enum class TetrominoType { I, O, T, S, Z, J, L };

class Tetromino {
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
    Tetromino(TetrominoType t) : type(t), rotation(0), x(WIDTH/2 - 2), y(0) { initShape(); }

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
    void setPosition(int newX, int newY) { x = newX; y = newY; }
};

// Grid Class
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
                    if (nx < 0 || nx >= WIDTH || ny >= HEIGHT)
                        return true;
                    if (ny >= 0 && grid[ny][nx] != "")
                        return true;
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
                    if (y >= 0)
                        grid[y][x] = t.getColor();
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

// Player Class
class Player {
private:
    Grid grid;
    Tetromino* current;
    string colorForGhost = ANSI_COLOR_GHOST;
    bool gameOverSoundPlayed = false; 

    Tetromino* newPiece() {
    // (This function is unchanged)
        TetrominoType types[] = {TetrominoType::I, TetrominoType::O, TetrominoType::T,
                                 TetrominoType::S, TetrominoType::Z, TetrominoType::J, TetrominoType::L};
        return new Tetromino(types[rand() % 7]);
    }

    void drawGhost(vector<vector<string>>& tempGrid) const {
    // (This function is unchanged)
        Tetromino* ghost = current->clone();
        while (!grid.isCollision(*ghost))
            ghost->move(0, 1);
        ghost->move(0, -1);
        for (size_t i = 0; i < ghost->getShape().size(); ++i) {
            for (size_t j = 0; j < ghost->getShape()[i].size(); ++j) {
                if (ghost->getShape()[i][j]) {
                    int x = ghost->getX() + j;
                    int y = ghost->getY() + i;
                    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
                        tempGrid[y][x] = colorForGhost;
                }
            }
        }
        delete ghost;
    }

public:
// (Public members unchanged)
    string name;
    int score;
    int level;
    bool gameOver;
    bool paused;
    int playerId;

    Player(int id, const string& n) : name(n), score(0), level(1),
        gameOver(false), paused(false), playerId(id) { current = newPiece(); }

    ~Player() { delete current; }

    void processCommand(const string& cmd) {
    // (This function is unchanged)
        if (paused) {
            if (cmd == "pause")
                paused = false;
            return;
        }
        Tetromino temp = *current;
        if (cmd == "L")           temp.move(-1, 0);
        else if (cmd == "R")      temp.move(1, 0);
        else if (cmd == "rotate") temp.rotate();
        else if (cmd == "soft")   temp.move(0, 1);
        else if (cmd == "hard") {
            while (!grid.isCollision(temp)) {
                current->move(0, 1);
                temp = *current;
            }
            current->move(0, -1);
        }
        else if (cmd == "pause") { paused = true; return; }
        else if (cmd == "quit") { gameOver = true; return; }
        if (!grid.isCollision(temp))
            *current = temp;
    }

    void update() {
        if (paused || gameOver) return;
        Tetromino temp = *current;
        temp.move(0, 1);
        if (grid.isCollision(temp)) {
            grid.merge(*current);
            int lines = grid.clearLines();
            score += lines * 100 * level;
            level += lines / 5;
            delete current;
            current = newPiece();
            const auto& gridData = grid.getGrid();
            for (int x = 0; x < WIDTH; x++) {
                if (gridData[0][x] != "") {
                    gameOver = true;
                    break;
                }
            }
            if (grid.isCollision(*current))
                gameOver = true;
        } else {
            *current = temp;
        }

        if (gameOver && !gameOverSoundPlayed) {
            // --- MODIFICATION: Use the absolute path ---
            string cmd = "aplay -q " + g_exe_dir_path + "/pop2.wav &";
            system(cmd.c_str());
            gameOverSoundPlayed = true;
        }
    }

    vector<string> render() const {
    // (This function is unchanged)
        vector<string> lines;
        stringstream ss;
        vector<vector<string>> tempGrid = grid.getGrid();
        {
            Tetromino* ghost = current->clone();
            while (!grid.isCollision(*ghost))
                ghost->move(0, 1);
            ghost->move(0, -1);
            for (size_t i = 0; i < ghost->getShape().size(); ++i) {
                for (size_t j = 0; j < ghost->getShape()[i].size(); ++j) {
                    if (ghost->getShape()[i][j]) {
                        int x = ghost->getX() + j;
                        int y = ghost->getY() + i;
                        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
                            tempGrid[y][x] = ANSI_COLOR_GHOST;
                    }
                }
            }
            delete ghost;
        }
        
        const auto& shape = current->getShape();
        int tx = current->getX();
        int ty = current->getY();
        for (size_t i = 0; i < shape.size(); ++i)
            for (size_t j = 0; j < shape[i].size(); ++j)
                if (shape[i][j]) {
                    int x = tx + j;
                    int y = ty + i;
                    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
                        tempGrid[y][x] = current->getColor();
                }
        
        string header = name + "  Score: " + to_string(score) + "  Level: " + to_string(level);
        int headerPad = (WIDTH*2 + 4 - header.length())/2;
        ss << string(headerPad > 0 ? headerPad : 0, ' ') << header;
        lines.push_back(ss.str());
        ss.str("");
        
        ss << ANSI_COLOR_WHITE << BLOCK;
        for (int x = 0; x < WIDTH; x++) ss << BLOCK;
        ss << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
        lines.push_back(ss.str());
        ss.str("");
        
        for (int y = 0; y < HEIGHT; ++y) {
            ss << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
            for (int x = 0; x < WIDTH; ++x) {
                if (tempGrid[y][x] != "") {
                    if (tempGrid[y][x] == ANSI_COLOR_GHOST)
                        ss << ANSI_COLOR_GHOST << GHOST << ANSI_COLOR_RESET;
                    else
                        ss << tempGrid[y][x] << BLOCK << ANSI_COLOR_RESET;
                } else {
                    ss << EMPTY;
                }
            }
            ss << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
            lines.push_back(ss.str());
            ss.str("");
        }
        
        ss << ANSI_COLOR_WHITE << BLOCK;
        for (int x = 0; x < WIDTH; x++) ss << BLOCK;
        ss << ANSI_COLOR_WHITE << BLOCK << ANSI_COLOR_RESET;
        lines.push_back(ss.str());
        
        if (paused) {
            lines.push_back("  PAUSED");
        }
        return lines;
    }
};

// --- MODIFICATION: New non-blocking input function ---
string getInput() {
    string input = "";
    char buf[16];
    int bytesRead;

    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        bytesRead = read(STDIN_FILENO, buf, sizeof(buf));
        if (bytesRead > 0) {
            input.append(buf, bytesRead);
        }
    }
    return input;
}

// MultiplayerGame Class
class MultiplayerGame {
private:
    Player player1;
    Player player2;
    bool globalQuit;
public:
    MultiplayerGame(const string& name1, const string& name2)
        : player1(1, name1), player2(2, name2), globalQuit(false) {
        srand(time(0));
    }

    void handleInput(const string& input) {
    // (This function is unchanged)
        size_t i = 0;
        while (i < input.size()) {
            char ch = input[i];
            if (ch == '\033' && i + 2 < input.size() && input[i+1]=='[') {
                char arrow = input[i+2];
                if (arrow == 'D') player2.processCommand("L");
                else if (arrow == 'C') player2.processCommand("R");
                else if (arrow == 'A') player2.processCommand("rotate");
                else if (arrow == 'B') player2.processCommand("soft");
                i += 3;
            } else {
                if (ch == 'a' || ch == 'A') {
                    player1.processCommand("L");
                } else if (ch == 'd' || ch == 'D') {
                    player1.processCommand("R");
                } else if (ch == 'w' || ch == 'W') {
                    player1.processCommand("rotate");
                } else if (ch == 's' || ch == 'S') {
                    player1.processCommand("soft");
                } else if (ch == ' ') {
                    player1.processCommand("hard");
                } else if (ch == '\n' || ch == '\r') {
                    player2.processCommand("hard");
                } else if (tolower(ch) == 'p') {
                    player1.processCommand("pause");
                    player2.processCommand("pause");
                } else if (ch == 'q' || ch == 27) {
                    player1.processCommand("quit");
                    player2.processCommand("quit");
                    globalQuit = true;
                }
                i++;
            }
        }
    }

    void draw() {
        // --- MODIFICATION: Use ANSI code instead of system("clear") ---
        cout << "\033[H\033[J"; 
        vector<string> board1 = player1.render();
        vector<string> board2 = player2.render();
        size_t maxLines = max(board1.size(), board2.size());
        for (size_t i = 0; i < maxLines; i++) {
            string line1 = (i < board1.size()) ? board1[i] : "";
            string line2 = (i < board2.size()) ? board2[i] : "";
            cout << line1 << "    " << line2 << "\n";
        }
        cout << "\nPress 'q' or ESC to quit.\n";
    }

    void update() {
    // (This function is unchanged)
        if (!player1.paused && !player1.gameOver) player1.update();
        if (!player2.paused && !player2.gameOver) player2.update();
    }

    bool isGameOver() {
        return globalQuit || (player1.gameOver && player2.gameOver);
    }

    void run() {
        // --- MODIFICATION: Set up signal handlers ---
        signal(SIGTSTP, handle_signal);
        signal(SIGCONT, handle_signal);

        // --- MODIFICATION: Enable raw mode ---
        enableRawMode();

        while (!isGameOver()) {
            draw();
            string inp = getInput(); // Use new non-blocking getInput
            if (!inp.empty()) handleInput(inp);
            update();
            usleep(300000 / ((player1.level + player2.level)/2 + 1));
        }

        // --- MODIFICATION: Disable raw mode *before* showing score ---
        disableRawMode();

        cout << "\033[H\033[J"; 
        cout << ANSI_COLOR_BOLD << ANSI_COLOR_RED << "GAME OVER!" << ANSI_COLOR_RESET << "\n";
        cout << player1.name << " Score: " << ANSI_COLOR_GREEN << player1.score << ANSI_COLOR_RESET << "\n";
        cout << player2.name << " Score: " << ANSI_COLOR_GREEN << player2.score << ANSI_COLOR_RESET << "\n";
        
        // --- MODIFICATION: Use the absolute path ---
        // (This only plays one sound, but it's safe)
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

    // --- MODIFICATION: Do all setup in normal mode *first* ---
    string name1, name2;
    cout << "\033[H\033[J"; 
    cout << ANSI_COLOR_BOLD << ANSI_COLOR_GREEN << "Welcome to TETRIS x2!" << ANSI_COLOR_RESET << endl;
    cout << "-----------------------" << endl;
    cout << "Enter Player 1 name (WASD & Spacebar): ";
    getline(cin, name1);
    cout << "Enter Player 2 name (Arrow Keys & Enter): ";
    getline(cin, name2);
    cout << "\n" << ANSI_COLOR_BOLD << "HOW TO PLAY:" << ANSI_COLOR_RESET << "\n"
              << "Player 1: " << ANSI_COLOR_YELLOW << "A/D" << ANSI_COLOR_RESET << " (Left/Right), " 
              << ANSI_COLOR_YELLOW << "W" << ANSI_COLOR_RESET << " (Rotate), " 
              << ANSI_COLOR_YELLOW << "S" << ANSI_COLOR_RESET << " (Soft Drop), "
              << ANSI_COLOR_YELLOW << "Space" << ANSI_COLOR_RESET << " (Hard Drop)\n"
              << "Player 2: " << ANSI_COLOR_YELLOW << "Arrows" << ANSI_COLOR_RESET << " (Left/Right/Down), "
              << ANSI_COLOR_YELLOW << "Up" << ANSI_COLOR_RESET << " (Rotate), "
              << ANSI_COLOR_YELLOW << "Enter" << ANSI_COLOR_RESET << " (Hard Drop)\n"
              << ANSI_COLOR_YELLOW << "P" << ANSI_COLOR_RESET << " - Pause, "
              << ANSI_COLOR_YELLOW << "Q/ESC" << ANSI_COLOR_RESET << " - Quit\n\n"
              << "Press " << ANSI_COLOR_GREEN << "Enter" << ANSI_COLOR_RESET << " to start...";
    
    char c;
    while(read(STDIN_FILENO, &c, 1) == 1 && c != '\n');

    MultiplayerGame game(name1, name2);
    game.run(); 
    
    return 0; 
}
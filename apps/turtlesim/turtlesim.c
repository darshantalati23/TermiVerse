#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>

#define WINDOW_SIZE 20
#define MAX_TICKS 100
#define CHART_WIDTH 60
#define CHART_HEIGHT 15
#define MAX_DATA_POINTS 500

// UI Colors
#define C_RESET   "\033[0m"
#define C_GREEN   "\033[1;32m"
#define C_RED     "\033[1;31m"
#define C_CYAN    "\033[1;36m"
#define C_YELLOW  "\033[1;33m"

typedef enum { FLAT, LONG, SHORT } PosType;

typedef struct {
    PosType pos;
    double entry_price;
    double realized_pnl;
    int trades;
    int wins;
    int losses;
    double returns[MAX_DATA_POINTS];
} Trader;

// Terminal mode state
struct termios orig_termios;

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios); }

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Math helpers
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Standard Box-Muller transform for normal distribution
double randn(double mu, double sigma) {
    double u1 = ((double)rand() / RAND_MAX);
    double u2 = ((double)rand() / RAND_MAX);
    double z0 = sqrt(-2.0 * log(u1 > 0 ? u1 : 1e-8)) * cos(2.0 * M_PI * u2);
    return z0 * sigma + mu;
}

// Instructions Screen
void show_instructions(int mode, const char* asset) {
    printf("\033[H\033[J"); // Clear screen
    printf(C_CYAN "=================================================\n");
    printf("        TurtleSim : Trading Simulator Game       \n");
    printf("=================================================\n" C_RESET);
    printf("\n" C_YELLOW "  [ RULES & OBJECTIVE ]" C_RESET "\n");
    printf("  Your goal is to beat the Donchian Channel Bot.\n");
    printf("  The Bot follows a simple trend-following strategy:\n");
    printf("  - Buys when price breaks the 20-period HIGH\n");
    printf("  - Sells when price breaks the 20-period LOW\n");
    
    printf("\n" C_YELLOW "  [ CONTROLS ]" C_RESET "\n");
    printf("  " C_GREEN "[B]" C_RESET " Buy / Long\n");
    printf("  " C_RED "[S]" C_RESET " Sell / Short\n");
    printf("  " C_YELLOW "[C]" C_RESET " Close Position\n");
    
    printf("\n" C_YELLOW "  [ DATASET INFO ]" C_RESET "\n");
    if (mode == 0) { // Live
        printf("  Source : " C_GREEN "Synthetic Brownian Motion" C_RESET "\n");
        printf("  Ticks  : %d simulated periods\n", MAX_TICKS);
    } else { // Backtest
        printf("  Asset  : " C_GREEN "%s" C_RESET "\n", asset);
        printf("  Source : " C_GREEN "Yahoo Finance (Real-world Data)" C_RESET "\n");
        printf("  Range  : " C_GREEN "Feb 12, 2026 to Jul 9, 2026" C_RESET "\n");
        printf("  Ticks  : ~100 daily closing prices\n");
    }
    
    printf("\n\n  Press " C_CYAN "[ENTER]" C_RESET " to begin the simulation...");
    fflush(stdout);
    
    char c;
    while(read(STDIN_FILENO, &c, 1) > 0) {
        if (c == '\n' || c == '\r') break;
    }
}

// 2D Chart rendering
void draw_chart_2d(double *history, double *upper_hist, double *lower_hist, char *human_actions, int current_t) {
    char grid[CHART_HEIGHT][CHART_WIDTH];
    char color_grid[CHART_HEIGHT][CHART_WIDTH];
    
    for(int r=0; r<CHART_HEIGHT; r++) {
        for(int c=0; c<CHART_WIDTH; c++) {
            grid[r][c] = ' ';
            color_grid[r][c] = 0; // 0=none, 1=cyan, 2=yellow, 3=green, 4=red
        }
    }
    
    int start_t = current_t - CHART_WIDTH + 1;
    if (start_t < 0) start_t = 0;
    
    double min_val = 9999999.0, max_val = -9999999.0;
    for(int t = start_t; t <= current_t; t++) {
        if(history[t] > max_val) max_val = history[t];
        if(history[t] < min_val) min_val = history[t];
        if(upper_hist[t] > max_val) max_val = upper_hist[t];
        if(lower_hist[t] < min_val) min_val = lower_hist[t];
    }
    
    if(max_val == min_val) { max_val += 1.0; min_val -= 1.0; }
    double range = max_val - min_val;
    
    for(int c=0; c < CHART_WIDTH; c++) {
        int t = start_t + c;
        if(t > current_t) break;
        
        int r_u = (int)((upper_hist[t] - min_val) / range * (CHART_HEIGHT - 1));
        int r_l = (int)((lower_hist[t] - min_val) / range * (CHART_HEIGHT - 1));
        int r_p = (int)((history[t] - min_val) / range * (CHART_HEIGHT - 1));
        
        if(r_u < 0) r_u = 0; if(r_u >= CHART_HEIGHT) r_u = CHART_HEIGHT-1;
        if(r_l < 0) r_l = 0; if(r_l >= CHART_HEIGHT) r_l = CHART_HEIGHT-1;
        if(r_p < 0) r_p = 0; if(r_p >= CHART_HEIGHT) r_p = CHART_HEIGHT-1;
        
        grid[r_u][c] = '-'; color_grid[r_u][c] = 1; 
        grid[r_l][c] = '-'; color_grid[r_l][c] = 1; 
        
        grid[r_p][c] = '*'; color_grid[r_p][c] = 2; 
        
        if(human_actions[t] == 'B') { grid[r_p][c] = '^'; color_grid[r_p][c] = 3; }
        if(human_actions[t] == 'S') { grid[r_p][c] = 'v'; color_grid[r_p][c] = 4; }
        if(human_actions[t] == 'C') { grid[r_p][c] = 'o'; color_grid[r_p][c] = 2; }
    }
    
    printf("\n");
    for(int r = CHART_HEIGHT - 1; r >= 0; r--) {
        double row_price = min_val + (r * range / (CHART_HEIGHT - 1));
        printf(" %7.2f | ", row_price);
        for(int c = 0; c < CHART_WIDTH; c++) {
            char ch = grid[r][c];
            int col = color_grid[r][c];
            if(col == 1) printf(C_CYAN "%c" C_RESET, ch);
            else if(col == 2) printf(C_YELLOW "%c" C_RESET, ch);
            else if(col == 3) printf(C_GREEN "%c" C_RESET, ch);
            else if(col == 4) printf(C_RED "%c" C_RESET, ch);
            else printf("%c", ch);
        }
        printf(" |\n");
    }
    printf("         +" );
    for(int c=0; c<CHART_WIDTH; c++) printf("-");
    printf("+\n");
}

// Trading execution
void execute_trade(Trader *t, double price, PosType new_pos) {
    if (t->pos != FLAT) {
        double pnl = (t->pos == LONG) ? (price - t->entry_price) : (t->entry_price - price);
        t->realized_pnl += pnl;
        t->returns[t->trades] = pnl / t->entry_price;
        t->trades++;
        if (pnl > 0) t->wins++; else t->losses++;
    }
    t->pos = new_pos;
    t->entry_price = (new_pos != FLAT) ? price : 0.0;
}

double get_unrealized(Trader *t, double price) {
    if (t->pos == FLAT) return 0.0;
    return (t->pos == LONG) ? (price - t->entry_price) : (t->entry_price - price);
}

void print_pos(Trader *t, double price, const char* name) {
    double unrlz = get_unrealized(t, price);
    double total = t->realized_pnl + unrlz;
    const char *pos_str = (t->pos == LONG) ? C_GREEN "LONG " C_RESET : (t->pos == SHORT) ? C_RED "SHORT" C_RESET : "FLAT ";
    printf("%-5s: Pos: %s | PnL: %s%7.2f" C_RESET "\n", name, pos_str, (total >= 0 ? C_GREEN : C_RED), total);
}

// Mode 1: Live Simulation
void run_live_sim() {
    show_instructions(0, "");
    
    enable_raw_mode();
    
    Trader bot = {FLAT, 0, 0, 0, 0, 0, {0}};
    Trader human = {FLAT, 0, 0, 0, 0, 0, {0}};
    
    double history[MAX_DATA_POINTS];
    double upper_hist[MAX_DATA_POINTS];
    double lower_hist[MAX_DATA_POINTS];
    char actions[MAX_DATA_POINTS];
    memset(actions, 0, sizeof(actions));
    
    double current_price = 100.0;
    
    for(int i = 0; i < WINDOW_SIZE; i++) {
        history[i] = current_price;
        upper_hist[i] = current_price;
        lower_hist[i] = current_price;
    }
    
    int t = WINDOW_SIZE;
    int end_t = t + MAX_TICKS;
    
    while (t < end_t && t < MAX_DATA_POINTS) {
        double log_return = randn(log(1.0005), 0.01);
        current_price *= exp(log_return);
        history[t] = current_price;
        
        double upper = history[t-1], lower = history[t-1];
        for (int i = t - WINDOW_SIZE; i < t; i++) {
            if (history[i] > upper) upper = history[i];
            if (history[i] < lower) lower = history[i];
        }
        double upper_10 = history[t-1], lower_10 = history[t-1];
        for (int i = t - 10; i < t; i++) {
            if (history[i] > upper_10) upper_10 = history[i];
            if (history[i] < lower_10) lower_10 = history[i];
        }
        upper_hist[t] = upper;
        lower_hist[t] = lower;
        
        struct timeval tv = {0, 300000}; 
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'b' || c == 'B') { execute_trade(&human, current_price, LONG); actions[t] = 'B'; }
                if (c == 's' || c == 'S') { execute_trade(&human, current_price, SHORT); actions[t] = 'S'; }
                if (c == 'c' || c == 'C') { execute_trade(&human, current_price, FLAT); actions[t] = 'C'; }
            }
        }
        
        if (bot.pos == LONG && current_price < lower_10) {
            execute_trade(&bot, current_price, FLAT);
        } else if (bot.pos == SHORT && current_price > upper_10) {
            execute_trade(&bot, current_price, FLAT);
        }
        
        if (bot.pos == FLAT) {
            if (current_price > upper) {
                execute_trade(&bot, current_price, LONG);
            } else if (current_price < lower) {
                execute_trade(&bot, current_price, SHORT);
            }
        }
        
        printf("\033[H\033[J"); // Clear screen
        printf(C_CYAN "=== TurtleSim : Live Brownian Motion Game ===" C_RESET "\n");
        draw_chart_2d(history, upper_hist, lower_hist, actions, t);
        printf("\n");
        print_pos(&human, current_price, "Human");
        print_pos(&bot, current_price, "Bot");
        
        t++;
    }
    
    disable_raw_mode();
    
    execute_trade(&bot, current_price, FLAT);
    execute_trade(&human, current_price, FLAT);
    
    printf("\n\n" C_CYAN "=== FINAL SCOREBOARD ===" C_RESET "\n");
    printf("Human -> PnL: %7.2f | Trades: %d | W/L: %d/%d\n", human.realized_pnl, human.trades, human.wins, human.losses);
    printf("Bot   -> PnL: %7.2f | Trades: %d | W/L: %d/%d\n", bot.realized_pnl, bot.trades, bot.wins, bot.losses);
    if (human.realized_pnl > bot.realized_pnl) printf(C_GREEN "You beat the Bot!\n" C_RESET);
    else printf(C_RED "The Bot wins!\n" C_RESET);
}

// Mode 2: Backtest
void run_backtest(const char* target_asset) {
    FILE *fp = fopen("apps/turtlesim/market_data.txt", "r");
    if (!fp) {
        printf("Error: Could not open apps/turtlesim/market_data.txt\n");
        return;
    }
    
    char line[4096];
    double history[MAX_DATA_POINTS];
    int n = 0;
    int found = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        char *token = strtok(line, ",");
        if (token && strcmp(token, target_asset) == 0) {
            found = 1;
            while ((token = strtok(NULL, ",")) != NULL && n < MAX_DATA_POINTS) {
                history[n++] = atof(token);
            }
            break;
        }
    }
    fclose(fp);
    
    if (!found) {
        printf("Asset '%s' not found in market_data.txt\n", target_asset);
        return;
    }
    
    show_instructions(1, target_asset);
    
    enable_raw_mode();
    
    Trader bot = {FLAT, 0, 0, 0, 0, 0, {0}};
    Trader human = {FLAT, 0, 0, 0, 0, 0, {0}};
    
    double upper_hist[MAX_DATA_POINTS];
    double lower_hist[MAX_DATA_POINTS];
    char actions[MAX_DATA_POINTS];
    memset(actions, 0, sizeof(actions));
    
    for(int i = 0; i < WINDOW_SIZE && i < n; i++) {
        upper_hist[i] = history[i];
        lower_hist[i] = history[i];
    }
    
    int t = WINDOW_SIZE;
    
    while (t < n) {
        double current_price = history[t];
        
        double upper = history[t-1], lower = history[t-1];
        for (int i = t - WINDOW_SIZE; i < t; i++) {
            if (history[i] > upper) upper = history[i];
            if (history[i] < lower) lower = history[i];
        }
        double upper_10 = history[t-1], lower_10 = history[t-1];
        for (int i = t - 10; i < t; i++) {
            if (history[i] > upper_10) upper_10 = history[i];
            if (history[i] < lower_10) lower_10 = history[i];
        }
        upper_hist[t] = upper;
        lower_hist[t] = lower;
        
        struct timeval tv = {0, 300000}; 
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'b' || c == 'B') { execute_trade(&human, current_price, LONG); actions[t] = 'B'; }
                if (c == 's' || c == 'S') { execute_trade(&human, current_price, SHORT); actions[t] = 'S'; }
                if (c == 'c' || c == 'C') { execute_trade(&human, current_price, FLAT); actions[t] = 'C'; }
            }
        }
        
        if (bot.pos == LONG && current_price < lower_10) {
            execute_trade(&bot, current_price, FLAT);
        } else if (bot.pos == SHORT && current_price > upper_10) {
            execute_trade(&bot, current_price, FLAT);
        }
        
        if (bot.pos == FLAT) {
            if (current_price > upper) {
                execute_trade(&bot, current_price, LONG);
            } else if (current_price < lower) {
                execute_trade(&bot, current_price, SHORT);
            }
        }
        
        printf("\033[H\033[J"); // Clear screen
        printf(C_CYAN "=== TurtleSim : Historical Backtest (%s) ===" C_RESET "\n", target_asset);
        draw_chart_2d(history, upper_hist, lower_hist, actions, t);
        printf("\n");
        print_pos(&human, current_price, "Human");
        print_pos(&bot, current_price, "Bot");
        
        t++;
    }
    
    disable_raw_mode();
    
    double final_price = history[n-1];
    execute_trade(&bot, final_price, FLAT);
    execute_trade(&human, final_price, FLAT);
    
    printf("\n\n" C_CYAN "=== FINAL SCOREBOARD ===" C_RESET "\n");
    printf("Human -> PnL: %7.2f | Trades: %d | W/L: %d/%d\n", human.realized_pnl, human.trades, human.wins, human.losses);
    printf("Bot   -> PnL: %7.2f | Trades: %d | W/L: %d/%d\n", bot.realized_pnl, bot.trades, bot.wins, bot.losses);
    if (human.realized_pnl > bot.realized_pnl) printf(C_GREEN "You beat the Bot!\n" C_RESET);
    else printf(C_RED "The Bot wins!\n" C_RESET);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    if (argc == 1 || strcmp(argv[1], "live") == 0) {
        run_live_sim();
    } else if (argc == 3 && strcmp(argv[1], "backtest") == 0) {
        run_backtest(argv[2]);
    } else {
        printf("Usage:\n");
        printf("  launch turtlesim live           - Run live game\n");
        printf("  launch turtlesim backtest <TKR> - Run backtest (e.g. AAPL, TSLA, GOLD)\n");
    }
    return 0;
}
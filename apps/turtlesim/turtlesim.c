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
#define CHART_WIDTH 50
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

// Chart rendering
void draw_chart(double price, double lower, double upper, double min_bound, double max_bound, char human_marker) {
    char line[CHART_WIDTH + 1];
    memset(line, '-', CHART_WIDTH);
    line[CHART_WIDTH] = '\0';
    
    double range = max_bound - min_bound;
    if (range == 0) range = 1.0;

    int l_idx = (int)((lower - min_bound) / range * (CHART_WIDTH - 1));
    int u_idx = (int)((upper - min_bound) / range * (CHART_WIDTH - 1));
    int p_idx = (int)((price - min_bound) / range * (CHART_WIDTH - 1));
    
    if (l_idx < 0) l_idx = 0; if (l_idx >= CHART_WIDTH) l_idx = CHART_WIDTH - 1;
    if (u_idx < 0) u_idx = 0; if (u_idx >= CHART_WIDTH) u_idx = CHART_WIDTH - 1;
    if (p_idx < 0) p_idx = 0; if (p_idx >= CHART_WIDTH) p_idx = CHART_WIDTH - 1;
    
    line[l_idx] = '[';
    line[u_idx] = ']';
    
    if (human_marker != ' ') line[p_idx] = human_marker;
    else line[p_idx] = '*';

    printf("%s | P: %7.2f | DC:[%6.2f, %6.2f]\n", line, price, lower, upper);
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
    printf("\033[H\033[J"); // Clear
    printf(C_CYAN "=== TurtleSim : Live Brownian Motion Game ===" C_RESET "\n");
    printf("Controls: " C_GREEN "[B]" C_RESET " Buy/Long  " C_RED "[S]" C_RESET " Sell/Short  " C_YELLOW "[C]" C_RESET " Close Position\n");
    printf("Goal: Beat the Donchian Channel Bot over %d ticks!\n\n", MAX_TICKS);
    
    enable_raw_mode();
    
    Trader bot = {FLAT, 0, 0, 0, 0, 0, {0}};
    Trader human = {FLAT, 0, 0, 0, 0, 0, {0}};
    
    double history[MAX_TICKS + 20];
    double current_price = 100.0;
    
    // Seed initial window to prevent immediate breakout
    for(int i = 0; i < WINDOW_SIZE; i++) {
        history[i] = current_price;
    }
    
    int t = WINDOW_SIZE;
    int end_t = t + MAX_TICKS;
    
    while (t < end_t) {
        // Brownian motion: drift=0.05, std=1.0
        current_price += randn(0.05, 1.0);
        history[t] = current_price;
        
        // Donchian channel calc (lookback previous WINDOW_SIZE periods)
        double upper = history[t-1], lower = history[t-1];
        for (int i = t - WINDOW_SIZE; i < t; i++) {
            if (history[i] > upper) upper = history[i];
            if (history[i] < lower) lower = history[i];
        }
        
        char human_marker = ' ';
        // Human input
        struct timeval tv = {0, 300000}; // 300ms per tick
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            read(STDIN_FILENO, &c, 1);
            if (c == 'b' || c == 'B') { execute_trade(&human, current_price, LONG); human_marker = '^'; }
            if (c == 's' || c == 'S') { execute_trade(&human, current_price, SHORT); human_marker = 'v'; }
            if (c == 'c' || c == 'C') { execute_trade(&human, current_price, FLAT); human_marker = 'o'; }
        }
        
        // Bot logic (Turtle)
        if (current_price > upper && bot.pos != LONG) execute_trade(&bot, current_price, LONG);
        else if (current_price < lower && bot.pos != SHORT) execute_trade(&bot, current_price, SHORT);
        
        // Render
        printf("\r\033[K"); // Clear line
        draw_chart(current_price, lower, upper, 80.0, 140.0, human_marker);
        print_pos(&human, current_price, "Human");
        print_pos(&bot, current_price, "Bot");
        printf("\033[2A\r"); // Move up 2 lines
        
        t++;
    }
    
    disable_raw_mode();
    
    // Close positions
    execute_trade(&bot, current_price, FLAT);
    execute_trade(&human, current_price, FLAT);
    
    printf("\n\n\n" C_CYAN "=== FINAL SCOREBOARD ===" C_RESET "\n");
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
    
    Trader bot = {FLAT, 0, 0, 0, 0, 0, {0}};
    double peak_pnl = 0.0, max_dd = 0.0;
    double min_price = history[0], max_price = history[0];
    
    for (int i = 0; i < n; i++) {
        if (history[i] > max_price) max_price = history[i];
        if (history[i] < min_price) min_price = history[i];
    }
    
    printf("\n" C_CYAN "--- Backtesting %s ---" C_RESET "\n", target_asset);
    
    for (int t = WINDOW_SIZE; t < n; t++) {
        double current_price = history[t];
        double upper = history[t-1], lower = history[t-1];
        
        for (int i = t - WINDOW_SIZE; i < t; i++) {
            if (history[i] > upper) upper = history[i];
            if (history[i] < lower) lower = history[i];
        }
        
        char marker = ' ';
        if (current_price > upper && bot.pos != LONG) {
            execute_trade(&bot, current_price, LONG);
            marker = '^';
        } else if (current_price < lower && bot.pos != SHORT) {
            execute_trade(&bot, current_price, SHORT);
            marker = 'v';
        }
        
        double current_pnl = bot.realized_pnl + get_unrealized(&bot, current_price);
        if (current_pnl > peak_pnl) peak_pnl = current_pnl;
        double dd = peak_pnl - current_pnl;
        if (dd > max_dd) max_dd = dd;
        
        draw_chart(current_price, lower, upper, min_price - 5, max_price + 5, marker);
    }
    
    execute_trade(&bot, history[n-1], FLAT);
    
    // Calc Sharpe
    double sum_ret = 0, sum_sq = 0;
    for(int i=0; i<bot.trades; i++) sum_ret += bot.returns[i];
    double mean_ret = bot.trades > 0 ? sum_ret / bot.trades : 0;
    for(int i=0; i<bot.trades; i++) sum_sq += pow(bot.returns[i] - mean_ret, 2);
    double std_ret = bot.trades > 1 ? sqrt(sum_sq / (bot.trades - 1)) : 1;
    double sharpe = (std_ret > 0) ? (mean_ret / std_ret) * sqrt(bot.trades) : 0; // rough annualized approx
    
    printf("\n" C_CYAN "--- BACKTEST RESULTS ---" C_RESET "\n");
    printf("Total Trades: %d\n", bot.trades);
    printf("Win/Loss: %d / %d\n", bot.wins, bot.losses);
    printf("Total Return (Abs): %.2f\n", bot.realized_pnl);
    printf("Max Drawdown: %.2f\n", max_dd);
    printf("Est. Sharpe Ratio: %.2f\n", sharpe);
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
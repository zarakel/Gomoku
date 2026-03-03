// cli_test.c — Headless Gomoku test harness (no MLX graphics)
// Build: make test_cli
// Usage: ./gomoku_test [random|defensive|aggressive|all] [num_games]
#include "../include/gomoku.h"
#include <time.h>
#include <string.h>

// ============================================================
// STUBS for graphical functions (not available in CLI mode)
// ============================================================
void drawSquare(screen *w, int x, int y, unsigned short int t) { (void)w;(void)x;(void)y;(void)t; }
void printBlack(screen *w) { (void)w; }
void putCadrillage(screen *w) { (void)w; }
void initGUI(screen *w) { (void)w; }
void printInformation(screen *w, game *g) { (void)w;(void)g; }
bool isIaTurn(int iaTurn, int turn) { return (iaTurn == turn); }

// ============================================================
// Validation: detect false positives in threat_counts[IDX_WIN]
// ============================================================
static bool has_actual_five(game *g, int player) {
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        int y0 = GET_Y(idx);
        int offsets[4] = {1, BOARD_SIZE, BOARD_SIZE + 1, BOARD_SIZE - 1};
        for (int d = 0; d < 4; d++) {
            int count = 1;
            for (int k = 1; k < 5; k++) {
                int ni = idx + offsets[d] * k;
                if (ni < 0 || ni >= MAX_BOARD) break;
                int ny = GET_Y(ni);
                if (d == 0 && ny != y0) break;
                if ((d == 2 || d == 3) && abs(ny - y0) != k) break;
                if (g->board[ni] != player) break;
                count++;
            }
            if (count >= 5) return true;
        }
    }
    return false;
}

static void validate_threat_counts(game *g, const char *context) {
    for (int p = P1; p <= P2; p++) {
        int tc = g->threat_counts[p][IDX_WIN];
        bool real = has_actual_five(g, p);
        if (tc > 0 && !real) {
            printf("!!! FALSE POSITIVE [%s]: threat_counts[P%d][IDX_WIN]=%d but no actual five-in-a-row!\n",
                   context, p, tc);
        }
        if (tc == 0 && real) {
            printf("!!! FALSE NEGATIVE [%s]: P%d has five-in-a-row but threat_counts[IDX_WIN]=0!\n",
                   context, p);
        }
    }
}

// ============================================================
// Human strategies
// ============================================================
typedef enum { STRAT_RANDOM, STRAT_DEFENSIVE, STRAT_AGGRESSIVE } Strategy;

// Random: pick a random empty cell near existing stones
static int human_random(game *g, int player) {
    (void)player;
    // Prefer cells from cand_list (near stones)
    if (g->cand_count > 0) {
        int attempts = 0;
        while (attempts < 100) {
            int ci = rand() % g->cand_count;
            int idx = g->cand_list[ci];
            if (g->board[idx] == EMPTY && !is_double_three(g, idx, player))
                return idx;
            attempts++;
        }
    }
    // Fallback: any empty cell
    for (int i = 0; i < MAX_BOARD; i++)
        if (g->board[i] == EMPTY && !is_double_three(g, i, player))
            return i;
    return -1;
}

// Defensive: block the best IA threat
static int human_defensive(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int best_idx = -1;
    int best_def = -1;

    for (int ci = 0; ci < g->cand_count; ci++) {
        int idx = g->cand_list[ci];
        if (g->board[idx] != EMPTY) continue;
        if (is_double_three(g, idx, player)) continue;

        // Score defensif = score que l'adversaire obtient si on ne bloque pas
        g->board[idx] = opponent;
        int def_score = get_point_score_fast(g, GET_X(idx), GET_Y(idx), opponent);
        g->board[idx] = EMPTY;

        // Also consider own offensive potential
        g->board[idx] = player;
        int atk_score = get_point_score_fast(g, GET_X(idx), GET_Y(idx), player);
        g->board[idx] = EMPTY;

        int combined = def_score * 2 + atk_score;  // Prioritize defense
        if (combined > best_def) {
            best_def = combined;
            best_idx = idx;
        }
    }
    return (best_idx != -1) ? best_idx : human_random(g, player);
}

// Aggressive: maximize own attack score
static int human_aggressive(game *g, int player) {
    int best_idx = -1;
    int best_atk = -1;
    int opponent = (player == P1) ? P2 : P1;

    for (int ci = 0; ci < g->cand_count; ci++) {
        int idx = g->cand_list[ci];
        if (g->board[idx] != EMPTY) continue;
        if (is_double_three(g, idx, player)) continue;

        g->board[idx] = player;
        int atk_score = get_point_score_fast(g, GET_X(idx), GET_Y(idx), player);
        g->board[idx] = EMPTY;

        // Small defense bonus
        g->board[idx] = opponent;
        int def_score = get_point_score_fast(g, GET_X(idx), GET_Y(idx), opponent);
        g->board[idx] = EMPTY;

        int combined = atk_score * 3 + def_score;  // Prioritize attack
        if (combined > best_atk) {
            best_atk = combined;
            best_idx = idx;
        }
    }
    return (best_idx != -1) ? best_idx : human_random(g, player);
}

static int play_human(game *g, int player, Strategy strat) {
    switch (strat) {
        case STRAT_DEFENSIVE:  return human_defensive(g, player);
        case STRAT_AGGRESSIVE: return human_aggressive(g, player);
        default:               return human_random(g, player);
    }
}

// ============================================================
// Game simulation
// ============================================================
typedef struct {
    int     winner;         // P1 (human) or P2 (AI), or 0 (draw/max moves)
    int     total_moves;    // Total moves played (both players)
    int     ia_moves;       // Moves played by IA only
    double  max_time;       // Worst-case IA move time
    double  avg_time;       // Average IA move time
    double  total_time;     // Sum of IA times
    int     max_depth;      // Max depth reached
    double  avg_depth;      // Average depth across IA moves
} GameResult;

// Minimal makeIaMove for CLI — same logic as the real one but with screen stubs
// We reuse the real makeIaMove since it calls finalize_move which calls drawSquare (stubbed)
static GameResult run_game(Strategy strat, int game_num, bool verbose) {
    game g;
    screen win;
    GameResult res = {0};

    // Init game state
    memset(&g, 0, sizeof(g));
    memset(&win, 0, sizeof(win));
    memset(g.board, EMPTY, sizeof(g.board));
    g.board_size = BOARD_SIZE;
    g.turn = P1;
    g.iaTurn = P2;  // IA plays as P2 (White)
    g.game_over = false;
    g.winner = 0;
    g.current_hash = 0;
    win.board_size = BOARD_SIZE;

    memset(transposition_table, 0, sizeof(transposition_table));
    cand_rebuild(&g);
    clear_heuristics();

    int max_total_moves = 80;  // 40 per side max = 80 total
    double ia_times[50];
    int ia_depths[50];
    int ia_move_idx = 0;

    for (int move = 0; move < max_total_moves && !g.game_over; move++) {
        if (g.turn == P1) {
            // Human plays
            int human_move = play_human(&g, P1, strat);
            if (human_move == -1) break;  // No valid move

            MoveUndo undo;
            apply_move(&g, human_move, P1, &undo);
            validate_threat_counts(&g, "after human move");
            checkVictoryCondition(&g);

            if (verbose)
                printf("  [Human] (%d,%d)\n", GET_X(human_move), GET_Y(human_move));

            if (g.game_over) break;
            g.turn = P2;
        } else {
            // IA plays
            clock_t t_start = clock();

            // Capture depth from printf output by reading debug_node_count difference
            long long nodes_before = debug_node_count;

            makeIaMove(&g, &win);

            double elapsed = (double)(clock() - t_start) / CLOCKS_PER_SEC;

            validate_threat_counts(&g, "after IA move");
            checkVictoryCondition(&g);

            if (ia_move_idx < 50) {
                ia_times[ia_move_idx] = elapsed;
                ia_depths[ia_move_idx] = ia_last_depth;
                ia_move_idx++;
            }

            if (elapsed > res.max_time) res.max_time = elapsed;
            res.total_time += elapsed;

            if (g.game_over) break;
            g.turn = P1;
        }
    }

    res.winner = g.winner;
    res.ia_moves = ia_move_idx;
    res.total_moves = g.stone_count;

    // Average time & depth
    if (ia_move_idx > 0) {
        res.avg_time = res.total_time / ia_move_idx;
        int depth_sum = 0;
        int depth_max = 0;
        for (int i = 0; i < ia_move_idx; i++) {
            depth_sum += ia_depths[i];
            if (ia_depths[i] > depth_max) depth_max = ia_depths[i];
        }
        res.avg_depth = (double)depth_sum / ia_move_idx;
        res.max_depth = depth_max;
    }

    if (verbose || true) {
        const char *strat_name = (strat == STRAT_RANDOM) ? "Random" :
                                 (strat == STRAT_DEFENSIVE) ? "Defensive" : "Aggressive";
        printf("Game %d [%s]: Winner=%s | Moves: %d total, %d IA | MaxTime: %.3fs | AvgTime: %.3fs | AvgDepth: %.1f | MaxDepth: %d\n",
               game_num, strat_name,
               (res.winner == P1) ? "HUMAN" : (res.winner == P2) ? "IA" : "DRAW/TIMEOUT",
               res.total_moves, res.ia_moves,
               res.max_time, res.avg_time, res.avg_depth, res.max_depth);
    }

    return res;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char **argv) {
    srand(time(NULL));
    init_zobrist();

    int num_games = 5;
    Strategy strats[] = { STRAT_RANDOM, STRAT_DEFENSIVE, STRAT_AGGRESSIVE };
    int num_strats = 3;
    bool run_all = true;
    int specific_strat = -1;

    if (argc >= 2) {
        if (strcmp(argv[1], "random") == 0) { specific_strat = 0; run_all = false; }
        else if (strcmp(argv[1], "defensive") == 0) { specific_strat = 1; run_all = false; }
        else if (strcmp(argv[1], "aggressive") == 0) { specific_strat = 2; run_all = false; }
    }
    if (argc >= 3) {
        num_games = atoi(argv[2]);
        if (num_games < 1) num_games = 1;
        if (num_games > 100) num_games = 100;
    }

    printf("=== GOMOKU CLI TEST HARNESS ===\n");
    printf("Games per strategy: %d\n\n", num_games);

    int total_wins = 0, total_losses = 0, total_draws = 0;
    double worst_time_ever = 0;
    double sum_avg_time = 0;
    double sum_avg_depth = 0;
    int sum_ia_moves = 0;
    int games_played = 0;

    for (int s = 0; s < num_strats; s++) {
        if (!run_all && s != specific_strat) continue;

        const char *sname = (s == 0) ? "RANDOM" : (s == 1) ? "DEFENSIVE" : "AGGRESSIVE";
        printf("--- Strategy: %s ---\n", sname);

        int wins = 0, losses = 0, draws = 0;

        for (int g = 0; g < num_games; g++) {
            GameResult r = run_game(strats[s], g + 1, false);
            if (r.winner == P2) wins++;
            else if (r.winner == P1) losses++;
            else draws++;

            if (r.max_time > worst_time_ever) worst_time_ever = r.max_time;
            sum_avg_time += r.avg_time;
            sum_avg_depth += r.avg_depth;
            sum_ia_moves += r.ia_moves;
            games_played++;
        }

        total_wins += wins;
        total_losses += losses;
        total_draws += draws;

        printf("  Results: %d wins / %d losses / %d draws (%.0f%% winrate)\n\n",
               wins, losses, draws,
               (num_games > 0) ? (wins * 100.0 / num_games) : 0);
    }

    printf("=== SUMMARY ===\n");
    printf("Total: %d wins / %d losses / %d draws | Winrate: %.1f%%\n",
           total_wins, total_losses, total_draws,
           (games_played > 0) ? (total_wins * 100.0 / games_played) : 0);
    printf("Worst single-move time: %.3fs\n", worst_time_ever);
    printf("Average time per IA move: %.3fs\n",
           (games_played > 0) ? (sum_avg_time / games_played) : 0);
    printf("Average IA moves per game: %.1f\n",
           (games_played > 0) ? ((double)sum_ia_moves / games_played) : 0);
    printf("Average depth per game: %.1f\n",
           (games_played > 0) ? (sum_avg_depth / games_played) : 0);

    return (total_losses > 0) ? 1 : 0;
}

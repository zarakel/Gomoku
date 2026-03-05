#ifndef GOMOKU_H
# define GOMOKU_H

// --- STANDARD LIBS ---
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>


#define TIMEOUT_CODE -99999999

// Vérifie si un pixel (x, y) est dans les limites de la fenêtre 'win'
// On cast en (int) pour éviter les warnings de comparaison signé/non-signé
#define IS_VALID_PIXEL(x, y, win) \
    ((x) >= 0 && (y) >= 0 && \
     (x) < (int)(win)->width && (y) < (int)(win)->height)

// Bouton Reset
#define BTN_X 700
#define BTN_Y 3
#define BTN_W 80
#define BTN_H 25

// Marges Graphiques (Réintégrées)
#define BOARD_MARGIN_LEFT 30
#define BOARD_MARGIN_TOP 30
#define BOARD_MARGIN_RIGHT 30
#define BOARD_MARGIN_BOTTOM 30

// --- PROJECT LIBS ---
#include "../../lib/MLX42/include/MLX42/MLX42.h"

// --- CONSTANTES TT ---
#define TT_SIZE (2 << 20) // ~2 Millions d'entrées
#define TT_EXACT 0
#define TT_LOWERBOUND 1
#define TT_UPPERBOUND 2

// --- CONSTANTES DE JEU ---
#define WIDTH 818
#define HEIGHT 818
#define BOARD_SIZE 19
#define MAX_BOARD (BOARD_SIZE * BOARD_SIZE)
#define WIN_LENGTH 5
#define MAX_CAPTURES 10 // 5 paires = Victoire

// Limites
#define MAX_DEPTH 30
#define TIME_LIMIT_MS 490 

// Valeurs cases
#define EMPTY 0
#define P1 1
#define P2 2
#define HINT 3
#define PREVIS 3

// --- MACROS ---
#define GET_INDEX(x, y) ((y) * BOARD_SIZE + (x))
#define GET_X(index) ((index) % BOARD_SIZE)
#define GET_Y(index) ((index) / BOARD_SIZE)
#define IS_VALID(x, y) ((x) >= 0 && (x) < BOARD_SIZE && (y) >= 0 && (y) < BOARD_SIZE)

// --- SCORES CORRIGÉS (DIVISÉS PAR 100 POUR ÉVITER OVERFLOW) ---
// Max Int: 2,147,483,647. 
// Win Score à 20M laisse de la place pour x100 accumulations sans crash.

#define SORT_WIN        20000000  // Alias pour le tri

// 1. Les Scores de base (Pour l'évaluation statique) - ON GARDE
#define WIN_SCORE       20000000 
#define OPEN_FOUR       10000000 
#define CLOSED_FOUR     5000000   
#define OPEN_THREE      2000000   
#define CLOSED_THREE    50000
#define OPEN_TWO        4000  // 2 pierres alignées, les 2 bouts ouverts (was 1000 : trop faible vs centralité)
#define CLOSED_TWO      300   // 2 pierres alignées, 1 bout ouvert (was 100)

// Seuils pour les menaces
#define THREAT_THRESHOLD 500000

// Bonus/Malus
#define CAPTURE_BONUS   50000     // Par paire capturée
#define CENTER_BONUS    50        // Proximité du centre

// --- CONSTANTES DE TRI (HIÉRARCHIE CORRIGÉE) ---

#define SORT_HASH          200000000 // Priorité absolue (Hash Move)

// 1. VICTOIRE IMMÉDIATE (Je gagne tout de suite)
#define SORT_WIN_IMMEDIATE 100000000 

// 2. SURVIE (Je vais mourir au prochain tour si je ne joue pas là)
// (Bloquer Open 4, Closed 4, ou Victoire par Capture)
#define SORT_BLOCK_WIN     50000000 

// 3. MENACES MAJEURES
#define SORT_WIN_CAPTURE   40000000 
#define SORT_THREAT_MAX    30000000 // Pour les menaces très fortes
#define SORT_FORK          18000000 // Double-fork offensif (fourchette)

// 4. AUTRES
#define SORT_CAPTURE       5000000
#define SORT_KILLER_1      500000
#define SORT_KILLER_2      400000

#define THREAT_LEVELS 7

#define IDX_WIN          6
#define IDX_OPEN_FOUR    5
#define IDX_CLOSED_FOUR  4
#define IDX_OPEN_THREE   3
#define IDX_CLOSED_THREE 2
#define IDX_OPEN_TWO     1  // <--- NOUVEAU (Menace naissante)
#define IDX_OTHERS       0

// --- STRUCTURES ---

typedef struct screen {
    mlx_t       *mlx;
    mlx_image_t *img;
    mlx_image_t *text_img;
    mlx_image_t *restart_text;
    uint32_t    width;
    uint32_t    height;
    double      x;
    double      y;
    bool        moved;
    bool        resized;
    bool        isClicked;
    bool        changed;
    int         board_size;
} screen;

typedef struct timer {
    bool running;
    struct timespec start_ts;
    double elapsed;
} timer;

typedef struct game {
    int     board[MAX_BOARD];
    int     captures[3];
    int     board_size;
    int     turn;
    int     iaTurn;
    int     winner;
    bool    game_over;
    timer   ia_timer;
    uint64_t current_hash;
    long long pos_score[3]; 
    int     threat_counts[3][THREAT_LEVELS]; 
    int     max_threat_level[3];
    bool    in_crisis;
    bool    crisis_immediate_win; // true si l'adversaire peut gagner EN 1 COUP (≠ open four)
    int     crisis_level;
    int     crisis_move_count;
    int     crisis_moves[10];
    // --- CANDIDATE SET INCRÉMENTAL ---
    // Maintenu en O(25) par apply_move/undo_move.
    // Remplace le scan bbox O(361) dans generate_moves → +2 ply.
    int8_t  cand_refcount[MAX_BOARD]; // Nb de pierres dans dist≤2 de chaque case
    bool    in_cand[MAX_BOARD];       // Cette case est-elle candidate ?
    int     cand_list[MAX_BOARD];     // Liste compacte des cases candidates
    int     cand_count;               // Taille de cand_list
    int     cand_pos[MAX_BOARD];      // Position de chaque case dans cand_list (O(1) remove)
    int     stone_count;              // Nb de pierres sur le plateau (maintenu incrémentalement)
    int     hint_idx;                 // Case du hint affiché (-1 = aucun)
} game;

typedef struct both {
    screen  *windows;
    game    *gameData;
} both;

typedef struct {
    int move_idx;
    int captured_indices[10];
    int captured_count;
    int prev_captures[3];
} MoveUndo;

typedef struct {
    uint64_t key;
    int depth;
    int value;
    int flag;
    int best_move;
} TTEntry;

typedef struct {
    int index;
    int score_estim;
    bool is_capture;
} MoveCandidate;

typedef struct {
    int move_idx;
    int score;
} MoveVCF;

// --- GLOBALES ---
extern uint64_t zobrist_table[MAX_BOARD][3];
extern TTEntry transposition_table[TT_SIZE];
extern int killer_moves[MAX_DEPTH][2];
extern int history_heuristic[MAX_BOARD];
extern long long debug_node_count;
extern long long debug_cutoff_count;
extern int ia_last_depth;

// --- PROTOTYPES ---

// graphicsUtils.c & hook.c & timer.c & utils.c
void    printBlack(screen *windows);
void    putCadrillage(screen *windows);
void    drawSquare(screen *windows, int x0, int y0, unsigned short int team);
void    initGUI(screen *windows);
void    keyhook(mlx_key_data_t keydata, void *param);
void    cursor(double xpos, double ypos, void *param);
void    resize(int32_t width, int32_t height, void *param);
void    mousehook(mouse_key_t button, action_t action, modifier_key_t mods, void *param);
void    launchTimer(timer *t);
void    resetTimer(timer *t);
bool    isIaTurn(int iaTurn, int turn);
void    resetGame(game *gameData, screen *windows);
void    printInformation(screen *windows, game *gameData);
void    checkVictoryCondition(game *gameData);

// captures.c
// Note: On unifie le nom ici pour éviter les conflits
int     count_potential_captures(game *g, int lx, int ly, int player); 
int     apply_captures_for_ai(game *g, int lx, int ly, int player, int *captured_indices_buffer);
int     count_vulnerable_pairs(game *g, int player);
int     find_capture_move(game *g, int player);
int     count_vulnerable_pairs_after_move(game *g, int idx, int player);

// heuristics.c 
int     evaluate_board(game *g, int player);
int     get_point_score(game *g, int x, int y, int player);
int     get_point_score_fast(game *g, int x, int y, int player);
bool    is_double_three(game *g, int idx, int player);
void    explain_double_three(game *g, int idx, int player);
void    refresh_board_stats(game *g);
int     get_threat_level(int score);
void    update_impacted_scores(game *g, int x, int y, bool remove_mode);

// ai.c
void    makeIaMove(game *gameData, screen *windows);
int     computeHintMove(game *g, int player);

// ai_data.c
void    init_zobrist();
void    clear_heuristics();
void    tt_save(uint64_t key, int depth, int val, int flag, int best_move);
TTEntry* tt_probe(uint64_t key);

// ai_logic.c
void    apply_move(game *g, int idx, int player, MoveUndo *undo);
void    undo_move(game *g, int player, MoveUndo *undo);
void    cand_rebuild(game *g);

// ai_moves.c
int     generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move);

// ai_search.c
int     negamax(game *g, int depth, int alpha, int beta, int player, clock_t start_time, bool null_allowed);

// ai_threats.c (dead code removed)

// ai_tactics.c (VCF)
bool    has_vcf_win(game *g, int attacker, int depth, int max_depth, clock_t start_time);
int     find_winning_vcf(game *g, int attacker);
bool    check_five_align(game *g, int idx, int player);
bool    is_move_capturable(game *g, int idx, int player);

// ai_multi_threat.c
int     compute_junction_potential(game *g, int idx, int player);
int     evaluate_line(game *g, int x, int y, int dx, int dy, int player);
int     compute_fork_value(game *g, int idx, int player);
int     count_created_threats(game *g, int idx, int player);

// ai_captures.c
int     find_best_capture_move(game *g, int player);

// ai_crisis.c
void    update_crisis_state(game *g, int ia_player);
int     find_best_defense_with_threat_space(game *g, int ia_player);
bool    is_winning_threat(game *g, int idx, int opponent);

#endif
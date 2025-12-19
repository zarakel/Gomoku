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

// --- PROJECT LIBS ---
#include "../../lib/MLX42/include/MLX42/MLX42.h"

// --- CONSTANTES TT ---
#define TT_SIZE (1 << 20) // ~1 Million d'entrées (Power of 2 pour rapidité)
#define TT_EXACT 0
#define TT_LOWERBOUND 1   // Alpha cutoff
#define TT_UPPERBOUND 2   // Beta cutoff

// --- CONSTANTES DE JEU & PERFORMANCE ---
#define WIDTH 818
#define HEIGHT 818

// Bouton Reset
#define BTN_X 430
#define BTN_Y 10
#define BTN_W 80
#define BTN_H 30

// Marges Graphiques (Réintégrées)
#define BOARD_MARGIN_LEFT 30
#define BOARD_MARGIN_TOP 30
#define BOARD_MARGIN_RIGHT 30
#define BOARD_MARGIN_BOTTOM 30

// Paramètres du plateau
#define BOARD_SIZE 19
#define MAX_BOARD (BOARD_SIZE * BOARD_SIZE) // 361 cases
#define WIN_LENGTH 5
#define MAX_CAPTURES 10 // 5 paires = Victoire

// Limites de temps et de profondeur
#define MAX_DEPTH 30
#define TIME_LIMIT_MS 450 // On garde une marge de sécurité (50ms) pour l'affichage

// Valeurs des cases (Optimisé pour lecture rapide)
#define EMPTY 0
#define P1 1     // Joueur 1 (Noir)
#define P2 2     // Joueur 2 (Blanc/IA)
#define PREVIS 3 // Prévisualisation

// --- MACROS (CRITIQUE POUR LA VITESSE) ---
#define GET_INDEX(x, y) ((y) * BOARD_SIZE + (x))
#define GET_X(index) ((index) % BOARD_SIZE)
#define GET_Y(index) ((index) / BOARD_SIZE)
#define IS_VALID(x, y) ((x) >= 0 && (x) < BOARD_SIZE && (y) >= 0 && (y) < BOARD_SIZE)

// --- POIDS DES SCORES (HIÉRARCHIE LOGARITHMIQUE) ---
// Chaque niveau est ~10x plus important que le précédent

#define WIN_SCORE       1000000000  // 10^9 - Victoire absolue
#define OPEN_FOUR       100000000   // 10^8 - Victoire au prochain tour
#define CLOSED_FOUR     10000000    // 10^7 - Force le blocage immédiat
#define OPEN_THREE      1000000     // 10^6 - Menace critique
#define CLOSED_THREE    100000      // 10^5 - Menace sérieuse
#define OPEN_TWO        10000       // 10^4 - Bon développement
#define CLOSED_TWO      1000        // 10^3 - Développement faible

// Bonus/Malus
#define CAPTURE_BONUS   50000       // Par paire capturée
#define CENTER_BONUS    500         // Proximité du centre

// Vérifie si un pixel (x, y) est dans les limites de la fenêtre 'win'
// On cast en (int) pour éviter les warnings de comparaison signé/non-signé
#define IS_VALID_PIXEL(x, y, win) \
    ((x) >= 0 && (y) >= 0 && \
     (x) < (int)(win)->width && (y) < (int)(win)->height)

// --- STRUCTURES ---

typedef struct screen
{
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

typedef struct timer
{
    bool running;
    struct timespec start_ts;
    double elapsed;
} timer;

typedef struct game
{
    int     board[MAX_BOARD];
    int     captures[3];
    int     score[3];
    int     board_size;
    int     turn;
    int     iaTurn;
    bool    game_over;
    timer   ia_timer;
    uint64_t current_hash;
} game;

typedef struct both
{
    screen  *windows;
    game    *gameData;
} both;

// Structure pour stocker ce qu'il faut annuler après un coup
typedef struct {
    int move_idx;           // Où a-t-on joué ?
    int captured_indices[10]; // Quels pions ont été retirés ? (Indices 1D)
    int captured_count;     // Combien de pions retirés ?
    int prev_score[3];      // Les scores heuristiques avant le coup
    int prev_captures[3];   // Les compteurs de capture avant le coup
} MoveUndo;

// --- STRUCTURE TT ---
typedef struct {
    uint64_t key;   // Zobrist Hash pour vérifier les collisions
    int depth;      // Profondeur de la recherche stockée
    int value;      // Score stocké
    int flag;       // Type de score (Exact, Upper, Lower)
    int best_move;  // Le meilleur coup trouvé pour cette position
} TTEntry;

// --- STRUCTURES IA ---

typedef struct {
    int index;
    int score_estim;
} MoveCandidate;

// --- GLOBALES IA (Déclarations extern) ---
extern uint64_t zobrist_table[MAX_BOARD][3];
extern TTEntry transposition_table[TT_SIZE];
extern int killer_moves[MAX_DEPTH][2];
extern int history_heuristic[MAX_BOARD];
extern long long debug_node_count;
extern long long debug_cutoff_count;

// --- PROTOTYPES ---

// graphicsUtils.c
int     get_rgba(int r, int g, int b, int a);
void    printBlack(screen *windows);
void    putCadrillage(screen *windows);
int     teamColor(unsigned short int team);
void    drawSquare(screen *windows, int x0, int y0, unsigned short int team);
void    initGUI(screen *windows);

// hook.c
void    keyhook(mlx_key_data_t keydata, void *param);
void    cursor(double xpos, double ypos, void *param);
void    resize(int32_t width, int32_t height, void *param);
void    mousehook(mouse_key_t button, action_t action, modifier_key_t mods, void *param);

// timer.c
void    stopTimer(timer *t);
void    launchTimer(timer *t);
void    resetTimer(timer *t);

// utils.c
bool    isIaTurn(int iaTurn, int turn);
void    resetGame(game *gameData, screen *windows);

// information.c
void    printInformation(screen *windows, game *gameData);

// captures.c
void    checkPieceCapture(game *gameData, screen *windows, int lx, int ly);
bool    in_bounds(int x, int y);
int     apply_captures_for_ai(game *g, int lx, int ly, int player, int *captured_indices_buffer);
int     count_potential_captures(game *g, int lx, int ly, int player);  // AJOUT

// victory.c
void    checkVictoryCondition(game *gameData);

// heuristics.c 
int     evaluate_board(game *g, int player);
int     get_point_score(game *g, int x, int y, int player);
bool    is_double_three(game *g, int idx, int player);
void    explain_double_three(game *g, int idx, int player);

// ai.c
void    makeIaMove(game *gameData, screen *windows);

// ai_data.c
void init_zobrist();
void clear_heuristics();
void tt_save(uint64_t key, int depth, int val, int flag, int best_move);
TTEntry* tt_probe(uint64_t key);

// ai_logic.c
void apply_move(game *g, int idx, int player, MoveUndo *undo);
void undo_move(game *g, int player, MoveUndo *undo);

// ai_moves.c
int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move);
int quick_evaluate_move(game *g, int idx, int player);

// ai_search.c
int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time);
int solve_vcf(game *g, int ia_player, clock_t start_time);
int vcf_search(game *g, int depth, int player, int ia_player, clock_t start_time);

#endif
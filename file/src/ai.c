#include "../include/gomoku.h"
#include <limits.h>

// --- Fonction de vérification manuelle (Anti-Zombie) ---
static bool check_real_win(game *g, int player) {
    // Scan horizontal, vertical, diagonal
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int idx = y * BOARD_SIZE + x;
            if (g->board[idx] != player) continue;

            int dirs[4][2] = {{1,0}, {0,1}, {1,1}, {1,-1}};
            for (int d = 0; d < 4; d++) {
                int count = 1;
                for (int k = 1; k < 5; k++) {
                    int nx = x + dirs[d][0] * k;
                    int ny = y + dirs[d][1] * k;
                    if (nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) break;
                    if (g->board[ny * BOARD_SIZE + nx] == player) count++;
                    else break;
                }
                if (count >= 5) return true;
            }
        }
    }
    if (g->captures[player] >= 5) return true;
    return false;
}

/* Exécute la recherche pour une profondeur donnée avec Aspiration Window */
static int run_aspiration_search(game *g, int depth, int prev_score, int *best_move_out, int ia_player, clock_t start) {
    // CORRECTION : Utiliser des bornes plus sûres
    int alpha = -WIN_SCORE - 10000;
    int beta = WIN_SCORE + 10000;
    int window = 500;
    
    // Initialisation de la fenêtre étroite
    if (depth > 2) {
        alpha = prev_score - window;
        beta = prev_score + window;
        
        // Clamp pour éviter les overflows
        if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
        if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
    }

    int loop_guard = -1; // Sécurité anti-boucle infinie

    while (++loop_guard < 3) {
        int current_best_idx = -1;
        int current_best_score = INT_MIN;
        bool time_out = false;
        
        // Root Move Generation
        MoveCandidate moves[MAX_BOARD];
        int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
        
        if (count == 0) return prev_score; // Pas de coups ? On garde le score précédent
        if (*best_move_out == -1) *best_move_out = moves[0].index;

        game working_game = *g; 

        for (int i = 0; i < count; i++) {
            int idx = moves[i].index;
            MoveUndo undo;
            apply_move(&working_game, idx, ia_player, &undo);
            
            // Appel Minimax
            int val = minimax(&working_game, depth - 1, alpha, beta, false, ia_player, start);
            
            undo_move(&working_game, ia_player, &undo);

            if (val == -2) { time_out = true; break; }
            
            if (val > current_best_score) {
                current_best_score = val;
                current_best_idx = idx;
            }
            
            // Alpha-Beta classique au niveau racine
            if (val > alpha) alpha = val;
            if (alpha >= beta) break; // Beta Cutoff
        }

        if (time_out) return -2; // Signal Timeout

        // --- ASPIRATION LOGIC (CORRIGÉE) ---
        // On vérifie si le score est sorti de la fenêtre initiale.
        // CORRECTION : On utilise '<' et '>' stricts pour éviter le fail sur égalité (0 <= 0)
        // Sauf si on est déjà en fenêtre large (INT_MIN/INT_MAX).
        
        bool fail_low = (current_best_score < alpha); // Était <=
        bool fail_high = (current_best_score > beta); // Était >=

        // Cas spécial : Si alpha est INT_MIN, on ne peut pas fail low
        if (alpha == INT_MIN) fail_low = false;
        // Cas spécial : Si beta est INT_MAX, on ne peut pas fail high
        if (beta == INT_MAX) fail_high = false;

        if (depth > 2 && (fail_low || fail_high)) {
            #ifdef DEBUG
            printf("Aspiration Fail at depth %d (Score %d outside [%d, %d]). Re-searching full window.\n", 
                   depth, current_best_score, alpha, beta);
            #endif
            
            // Si on a déjà tout ouvert, on arrête (évite la boucle infinie)
            if (alpha <= INT_MIN + 1000 && beta >= INT_MAX - 1000) {
                *best_move_out = current_best_idx;
                return current_best_score;
            }

            // Sinon, on ouvre grand les vannes et on recommence
            alpha = INT_MIN; 
            beta = INT_MAX;
            continue; 
        }

        *best_move_out = current_best_idx;
        return current_best_score;
    }
}

/* Orchestre l'Iterative Deepening */
static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_score = 0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        
        if (score == -2) { // Timeout
            #ifdef DEBUG
            printf("Timeout at depth %d. Keeping best move from depth %d.\n", depth, depth-2);
            #endif
            break;
        }

        prev_score = score;
        #ifdef DEBUG
        printf("Depth %d complete. Best: %d. Nodes: %lld, Cutoffs: %lld.\n", depth, score, debug_node_count, debug_cutoff_count);
        #endif

        if (score > WIN_SCORE - 5000) {
            #ifdef DEBUG
            printf("Winning move found at depth %d.\n", depth);
            #endif
            break;
        }
        if ((clock() - start) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) break;
    }
    return best_move;
}

/* Applique le coup final et met à jour l'UI */
static void finalize_move(game *g, screen *win, int move_idx, int player, clock_t start, bool is_vcf) {
    if (move_idx != -1) {
        int x = GET_X(move_idx);
        int y = GET_Y(move_idx);
        
        MoveUndo final_undo;
        apply_move(g, move_idx, player, &final_undo);

        drawSquare(win, x, y, player);
        if (final_undo.captured_count > 0) {
            for (int k = 0; k < final_undo.captured_count; k++) {
                int cap_idx = final_undo.captured_indices[k];
                drawSquare(win, GET_X(cap_idx), GET_Y(cap_idx), EMPTY);
            }
        }
        win->changed = true; 
        
        if (is_vcf) printf(">>> IA joue le coup VCF/WIN en (%d, %d) [Temps: %.3fs]\n", x, y, (double)(clock() - start) / CLOCKS_PER_SEC);
        else printf("IA plays at (%d, %d) [Temps: %.3fs]\n", x, y, (double)(clock() - start) / CLOCKS_PER_SEC);
    } else {
        printf("IA cannot move.\n");
    }
    g->ia_timer.elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
}

void makeIaMove(game *gameData, screen *windows) {
    int opponent = (gameData->turn == P1) ? P2 : P1;
    
    // --- SÉCURITÉ RENFORCÉE ---
    // On ne fait pas confiance au score incrémental pour la défaite.
    // On scanne le plateau.
    if (check_real_win(gameData, opponent)) {
        printf(">>> L'IA voit (scan réel) qu'elle a perdu. Elle ne joue pas.\n");
        return;
    }
    // --------------------------

    clock_t start = clock();
    int ia_player = gameData->turn;
    
    // 1. VCF / Victoire Immédiate
    int best_move = solve_vcf(gameData, ia_player, start);
    bool is_vcf = (best_move != -1);

    // 2. Minimax (Si pas de VCF)
    if (!is_vcf) {
        best_move = run_iterative_deepening(gameData, ia_player, start);
    }

    //3. Application
    finalize_move(gameData, windows, best_move, ia_player, start, is_vcf);
}

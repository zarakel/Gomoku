#include "../include/gomoku.h"
#include <limits.h>

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
    return prev_score;
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
    launchTimer(&gameData->ia_timer);
    clock_t start = clock();

    int ia_player = gameData->turn;
    int opponent = (ia_player == P1) ? P2 : P1;
    
    #ifdef DEBUG
    printf("\n========== IA TURN ==========\n");
    
    // Afficher toutes les lignes de 3+ pierres adverses
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    char* dir_names[] = {"H", "V", "D\\", "D/"};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (gameData->board[idx] != opponent) continue;
        int x = GET_X(idx), y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            // Compter pierres dans cette direction
            int stones = 1;
            for (int k = 1; k < 5; k++) {
                int nx = x + dx[d]*k, ny = y + dy[d]*k;
                if (!IS_VALID(nx, ny)) break;
                if (gameData->board[GET_INDEX(nx, ny)] == opponent) stones++;
                else break;
            }
            for (int k = 1; k < 5; k++) {
                int nx = x - dx[d]*k, ny = y - dy[d]*k;
                if (!IS_VALID(nx, ny)) break;
                if (gameData->board[GET_INDEX(nx, ny)] == opponent) stones++;
                else break;
            }
            
            if (stones >= 3) {
                printf("ALERTE: %d pierres adverses en %s depuis (%d,%d)\n",
                       stones, dir_names[d], x, y);
            }
        }
    }
    #endif
    
    /* Phase 1 : Décision tactique (remplace solve_vcf) */
    int best_move = make_tactical_decision(gameData, ia_player, start);
    bool is_tactical = (best_move != -1);

    /* Phase 2 : Minimax si pas de décision tactique */
    if (!is_tactical) {
        best_move = run_iterative_deepening(gameData, ia_player, start);
    }

    /* Applique le coup final et met à jour l'UI */
    finalize_move(gameData, windows, best_move, ia_player, start, is_tactical);
}

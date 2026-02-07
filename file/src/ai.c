#include "../include/gomoku.h"
#include <limits.h>

/**
 * Recherche avec fenetre d'aspiration pour optimiser alpha-beta.
 * Utilise le score de l'iteration precedente pour definir une fenetre etroite [alpha, beta].
 * Si le score sort de cette fenetre, on elargit et on relance la recherche.
 * Retourne le meilleur score trouve, ou TIMEOUT_CODE si le temps est ecoule.
 */
static int run_aspiration_search(game *g, int depth, int prev_score, int *best_move_out, int ia_player, clock_t start) {
    int alpha = -WIN_SCORE - 10000;
    int beta = WIN_SCORE + 10000;
    // Fenetre initiale large pour eviter les echecs de recherche
    int window = 5000;
    
    // Adaptation de la fenetre selon le score precedent
    // Plus le score est instable (grand), plus on elargit la fenetre
    if (depth > 2 && abs(prev_score) < WIN_SCORE - 10000) {
        // Fenêtre adaptative : plus large si score instable
        window = 5000 + (abs(prev_score) / 100); // Croissance progressive
        alpha = prev_score - window;
        beta = prev_score + window;
        if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
        if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
    }

    int loop_guard = 0;
    while (loop_guard++ < 3) {
        int alpha_origin = alpha;
        int beta_origin = beta;
        int current_best_idx = -1;
        int current_best_score = INT_MIN;
        bool time_out = false;
        
        MoveCandidate moves[MAX_BOARD];
        int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
        if (count == 0) return prev_score; 
        if (*best_move_out == -1) *best_move_out = moves[0].index;

        for (int i = 0; i < count; i++) {
            int idx = moves[i].index;
            MoveUndo undo;
            apply_move(g, idx, ia_player, &undo);
            int val = minimax(g, depth - 1, alpha, beta, false, ia_player, start);
            undo_move(g, ia_player, &undo);

            if (val == TIMEOUT_CODE) { time_out = true; break; }
            if (val > current_best_score) {
                current_best_score = val;
                current_best_idx = idx;
            }
            if (val > alpha) alpha = val;
            if (alpha >= beta) { debug_cutoff_count++; break; }
        }

        if (time_out) return TIMEOUT_CODE; 
        if (depth > 2 && (current_best_score < alpha_origin || current_best_score > beta_origin)) {
            alpha = -WIN_SCORE - 10000; 
            beta = WIN_SCORE + 10000;
            continue; 
        }
        *best_move_out = current_best_idx;
        return current_best_score;
    }
    return prev_score;
}

/**
 * Approfondissement iteratif : lance des recherches successives a profondeur croissante.
 * Chaque iteration beneficie des resultats precedents (table de transposition).
 * S'arrete en cas de timeout ou si une victoire est detectee.
 * Retourne l'index du meilleur coup trouve.
 */
static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_best_move = -1;
    int prev_score = 0;
    double allocated_time = (double)TIME_LIMIT_MS / 1000.0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {
        if (best_move != -1) prev_best_move = best_move;
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        if (score == TIMEOUT_CODE) return (prev_best_move != -1) ? prev_best_move : best_move;

        prev_score = score;
        printf("Depth %d complete. Score: %d\n", depth, score);
        
        // Arret anticipe si victoire imminente ou si 60% du temps est consomme
        // Permet de garder du temps pour finaliser le coup
        if (score > WIN_SCORE - 5000 || (double)(clock() - start) / CLOCKS_PER_SEC > allocated_time * 0.60) break;
    }
    return best_move;
}

/**
 * Applique le coup choisi sur le plateau et met a jour l'affichage.
 * Gere l'application du coup, l'affichage des captures, et les logs de temps.
 */
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
        printf("IA plays at (%d, %d) [%.3fs]\n", x, y, (double)(clock() - start) / CLOCKS_PER_SEC);
        g->ia_timer.elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    }
}

/**
 * Point d'entree principal pour le calcul du coup de l'IA.
 * 
 * Strategie en 2 phases :
 * 1. Detection des coups forces (victoire immediate ou blocage mortel)
 * 2. Si aucun coup force : recherche VCF puis minimax avec approfondissement iteratif
 * 
 * Inclut des mecanismes de secours pour garantir un coup valide meme en cas de timeout.
 */
void makeIaMove(game *gameData, screen *windows) {
    launchTimer(&gameData->ia_timer);
    clock_t start = clock();
    int ia_player = gameData->turn;
    int opponent = (ia_player == P1) ? P2 : P1;
    int best_move = -1;
    bool forcing_found = false;
    char *reason = "Minimax";

    // Mise a jour des statistiques du plateau (scores, menaces)
    refresh_board_stats(gameData);
    
    // Calcul du niveau de crise (informatif uniquement, ne force plus les coups)
    update_crisis_state(gameData, ia_player);

    // PHASE 1 : GENERATION ET TRI DES COUPS CANDIDATS
    // generate_moves explore le plateau et trie les coups par pertinence
    // (victoires immediates, menaces, captures, centralite)
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(gameData, moves, ia_player, 0, -1);
    
    if (count == 0) {
        // Plateau vide impossible en milieu de partie, mais par sécurité
        best_move = GET_INDEX(9, 9);
        forcing_found = true;
        reason = "Centre (plateau vide)";
    } else {
        // Analyse du meilleur coup genere (deja trie par score)
        // On ne force que les situations de victoire/mort immediate
        // Tout le reste (attaques, defenses) est evalue par minimax
        if (moves[0].score_estim >= SORT_WIN_IMMEDIATE) {
            best_move = moves[0].index;
            forcing_found = true;
            
            // Déterminer si c'est une victoire ou un blocage
            gameData->board[best_move] = ia_player;
            int my_score = get_point_score(gameData, GET_X(best_move), GET_Y(best_move), ia_player);
            gameData->board[best_move] = EMPTY;
            
            if (my_score >= WIN_SCORE) {
                reason = "Victoire immédiate";
            } else {
                reason = "Blocage menace mortelle"; // Seulement WIN_SCORE adverse
            }
        }
        // Aucun coup force detecte : minimax decidera
    }

    // PHASE 2 : RECHERCHE TACTIQUE ET STRATEGIQUE
    // Priorite 1 : VCF (sequences de menaces forcees menant a la victoire)
    // Priorite 2 : Minimax avec approfondissement iteratif (evaluation complete)
    if (!forcing_found) {
        int vcf_move = find_winning_vcf(gameData, ia_player);
        if (vcf_move != -1 && !is_double_three(gameData, vcf_move, ia_player)) {
            best_move = vcf_move; reason = "VCF Gagnant";
        } else {
            best_move = run_iterative_deepening(gameData, ia_player, start);
            // Sécurité anti-abandon : utiliser le meilleur coup défensif si position désespérée
            if (best_move == -1 || evaluate_board(gameData, ia_player) < -WIN_SCORE / 2) {
                // Utiliser les coups déjà générés et triés
                for (int i = 0; i < count && i < 5; i++) {
                    if (!is_double_three(gameData, moves[i].index, ia_player)) {
                        best_move = moves[i].index;
                        reason = "Défense de dernier recours";
                        break;
                    }
                }
            }
        }
    }

    double time_spent = (double)(clock() - start) / CLOCKS_PER_SEC;

    if (best_move != -1) {
        printf("✅ [%s] (%d, %d). Raison: %s [Temps: %.3fs]\n", 
               forcing_found ? "FORCED" : "AI", 
               GET_X(best_move), GET_Y(best_move), 
               reason, 
               time_spent);
    }

    // --- VALIDATION FINALE & SECOURS ---
    if (best_move == -1 || is_double_three(gameData, best_move, ia_player)) {
        // Utiliser les coups pré-générés comme fallback ultime
        for (int i = 0; i < count; i++) {
            if (!is_double_three(gameData, moves[i].index, ia_player)) {
                best_move = moves[i].index;
                reason = "Fallback sécurisé";
                break;
            }
        }
    }

    printf("✅ [%s] (%d, %d). Raison: %s\n", forcing_found ? "FORCED" : "AI", GET_X(best_move), GET_Y(best_move), reason);
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}
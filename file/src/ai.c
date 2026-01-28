#include "../include/gomoku.h"
#include <limits.h>

static int run_aspiration_search(game *g, int depth, int prev_score, int *best_move_out, int ia_player, clock_t start) {
    // CORRECTION : Utiliser des bornes plus sûres et larges par défaut
    int alpha = -WIN_SCORE - 10000;
    int beta = WIN_SCORE + 10000;
    int window = 500;
    
    // Initialisation de la fenêtre étroite autour du score précédent
    if (depth > 2) {
        alpha = prev_score - window;
        beta = prev_score + window;
        
        // Clamp pour éviter les overflows et rester dans la logique du jeu
        if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
        if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
    }

    int loop_guard = -1; // Sécurité anti-boucle infinie

    while (++loop_guard < 3) {
        // --- SAUVEGARDE DES BORNES D'ORIGINE ---
        int alpha_origin = alpha;
        int beta_origin = beta;

        int current_best_idx = -1;
        int current_best_score = INT_MIN;
        bool time_out = false;
        
        // Root Move Generation
        MoveCandidate moves[MAX_BOARD];
        int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
        
        if (count == 0) return prev_score; 
        if (*best_move_out == -1) *best_move_out = moves[0].index;

        game working_game = *g; 

        for (int i = 0; i < count; i++) {
            int idx = moves[i].index;
            MoveUndo undo;
            apply_move(&working_game, idx, ia_player, &undo);
            
            // Appel Minimax avec la fenêtre courante
            int val = minimax(&working_game, depth - 1, alpha, beta, false, ia_player, start);
            
            undo_move(&working_game, ia_player, &undo);

            if (val == -2) { time_out = true; break; }
            
            if (val > current_best_score) {
                current_best_score = val;
                current_best_idx = idx;
            }
            
            // Alpha-Beta classique au niveau racine (Alpha monte !)
            if (val > alpha) alpha = val;
            if (alpha >= beta) break; // Beta Cutoff
        }

        if (time_out) return -2; 

        // --- ASPIRATION LOGIC (CORRIGÉE) ---
        bool fail_low = (current_best_score < alpha_origin); 
        bool fail_high = (current_best_score > beta_origin);

        // Cas spécial : Si la fenêtre était déjà maximale, on ne peut pas fail
        if (alpha_origin <= -WIN_SCORE - 9000) fail_low = false;
        if (beta_origin >= WIN_SCORE + 9000) fail_high = false;

        if (depth > 2 && (fail_low || fail_high)) {
            #ifdef DEBUG
            printf("Aspiration Fail at depth %d. Score %d outside [%d, %d]. Re-searching full window.\n", 
                   depth, current_best_score, alpha_origin, beta_origin);
            #endif
            
            // Si on a déjà tout ouvert, on s'arrête là
            if (alpha_origin <= -WIN_SCORE - 9000 && beta_origin >= WIN_SCORE + 9000) {
                *best_move_out = current_best_idx;
                return current_best_score;
            }

            // Sinon, on ouvre grand les vannes et on recommence
            alpha = -WIN_SCORE - 10000; 
            beta = WIN_SCORE + 10000;
            continue; 
        }

        *best_move_out = current_best_idx;
        return current_best_score;
    }
    return prev_score;
}

/* Orchestre l'Iterative Deepening avec Gestion Dynamique du Temps */
static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_best_move = -1; // Sauvegarde de sécurité
    int prev_score = 0;

    // Conversion du budget temps en secondes (ex: 450ms -> 0.45s)
    double allocated_time = (double)TIME_LIMIT_MS / 1000.0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {

        // Sauvegarde le meilleur coup de la profondeur précédente valide
        if (best_move != -1) prev_best_move = best_move;
        
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        
        if (score == -2) { // Timeout PENDANT la recherche
            #ifdef DEBUG
            printf("Timeout during depth %d. Reverting to best move from depth %d.\n", depth, depth-2);
            #endif
            if (prev_best_move != -1) return prev_best_move;
            return best_move;
        }

        prev_score = score;
        #ifdef DEBUG
        printf("Depth %d complete. Best: %d. Nodes: %lld, Cutoffs: %lld.\n", depth, score, debug_node_count, debug_cutoff_count);
        #endif

        if (score > WIN_SCORE - 5000) {
            #ifdef DEBUG
            printf("Potential winning move found at depth %d. Verifying...\n", depth);
            #endif
            
            // OPTIMISATION : Si on trouve une victoire très tôt (depth < 10), 
            // on continue quand même un peu pour être sûr que ce n'est pas une gaffe
            // due à un horizon effect (l'adversaire a une menace juste après).
            if (depth < 12 && (double)(clock() - start) / CLOCKS_PER_SEC < allocated_time * 0.3) {
                // On a encore beaucoup de temps, on continue de creuser pour confirmer
                prev_score = score;
                continue; 
            }
            
            // Si c'est profond ou qu'on manque de temps, on accepte la victoire
            break;
        }

        // --- GESTION DYNAMIQUE DU TEMPS ---
        double time_taken = (double)(clock() - start) / CLOCKS_PER_SEC;
        
        // AVANT : if (time_taken > allocated_time / 2.0) break;
        // C'était prudent (facteur de branchement estimé à 2, ce qui est faible).
        
        // OPTIMISATION : On peut pousser un peu plus.
        // Si on a utilisé moins de 60% du temps, on tente la profondeur suivante.
        if (time_taken > allocated_time * 0.60) {
            #ifdef DEBUG
            printf("Stopping ID: Time used %.3fs / %.3fs\n", time_taken, allocated_time);
            #endif
            break;
        }
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
        
        if (is_vcf) {
            printf(">>> IA joue le coup VCF/WIN ! Score estimé : %d\n", evaluate_board(g, player));
        }        
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
    int best_move = -1;

    // === CORRECTION CRITIQUE 1 : MISE À JOUR IMMÉDIATE ===
    // On met à jour les menaces (Open 3, 4...) basées sur le dernier coup adverse
    refresh_board_stats(gameData);
    // =====================================================

    // --- PHASE 0 : VICTOIRE IMMÉDIATE ---
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(gameData, moves, ia_player, 0, -1);
    
    for (int i = 0; i < count && i < 5; i++) {
        if (moves[i].score_estim >= WIN_SCORE - 10000) {
            if (is_double_three(gameData, moves[i].index, ia_player)) {
                printf(">>> Coup (%d,%d) ignoré (Double-Three interdit)\n", 
                       GET_X(moves[i].index), GET_Y(moves[i].index));
                continue; 
            }
            best_move = moves[i].index;
            printf(">>> VICTOIRE IMMÉDIATE DÉTECTÉE (Coup Légal).\n");
            goto play_move;
        }
    }

    // --- PHASE 0.1 : VICTOIRE IMMÉDIATE PAR CAPTURE ---
    MoveCandidate cap_moves[MAX_BOARD];
    int cap_count = generate_moves(gameData, cap_moves, ia_player, 0, -1);
    for (int i = 0; i < cap_count; i++) {
        if (cap_moves[i].is_capture) {
            int captures = check_capture_count(gameData, cap_moves[i].index, ia_player);
            if (gameData->captures[ia_player] + captures >= 5) {
                 best_move = cap_moves[i].index;
                 printf(">>> VICTOIRE PAR CAPTURE DÉTECTÉE (Coup Fatal).\n");
                 goto play_move;
            }
        }
    }

    // --- PHASE 0.2 : DÉFAITE IMMÉDIATE PAR CAPTURE ---
    int opponent = (ia_player == P1) ? P2 : P1;
    if (gameData->captures[opponent] >= 4) {
        if (count_vulnerable_pairs(gameData, ia_player) > 0) {
             printf(">>> DANGER CRITIQUE : Adversaire va gagner par capture !\n");
             // On force la main à la phase de crise pour qu'elle trouve une parade
             gameData->in_crisis = true; 
             gameData->crisis_level = 3; 
        }
    }

    // --- PHASE 0.5 : VCF OFFENSIF SÉCURISÉ ---
    if (gameData->max_threat_level[ia_player] >= IDX_OPEN_THREE) {
        int winning_move = find_winning_vcf(gameData, ia_player);
        if (winning_move != -1) {
            bool safe = true;
            if (is_double_three(gameData, winning_move, ia_player)) safe = false;
            
            if (safe && gameData->captures[opponent] >= 4) {
                if (!check_five_align(gameData, winning_move, ia_player)) {
                    if (is_move_capturable(gameData, winning_move, ia_player)) {
                        safe = false; 
                        printf(">>> VCF ANNULÉ : Coup suicidaire (Capture) !\n");
                    }
                }
            }

            if (safe) {
                best_move = winning_move;
                printf(">>> VCF OFFENSIF TROUVÉ ! Victoire forcée enclenchée.\n");
                goto play_move;
            }
        }
    }

    // === CORRECTION CRITIQUE 2 : REORDONNANCEMENT ===
    // La Phase 1 (Survie/Crise) DOIT passer AVANT la Phase 0.6 (Fourchette)
    // On ne bloque pas une fourchette si on a un pistolet sur la tempe (Open 4).

    // --- PHASE 1 : FILET DE SÉCURITÉ (DEFENSIVE STRICT VCF) ---
    int opp_threat_level = (ia_player == P1) ? gameData->max_threat_level[P2] : gameData->max_threat_level[P1];
    
    // Si menace forte OU crise détectée précédemment (ex: captures)
    if (opp_threat_level >= IDX_OPEN_THREE || gameData->in_crisis) {
        int save_move = solve_defensive_crisis(gameData, ia_player);
        
        if (save_move >= 0) {
            best_move = save_move;
            printf(">>> IA joue le coup de SAUVETAGE (VCF Block).\n");
            goto play_move;
        } else if (save_move == -2) {
            printf(">>> Mode Survie (Désespéré).\n");
        }
    }

    // --- PHASE 0.6 : DÉTECTION FOURCHETTE ADVERSE (Maintenant après la survie) ---
    // On ne cherche à bloquer les fourchettes que si on n'est PAS en crise mortelle
    if (!gameData->in_crisis) {
        MoveCandidate opp_moves[MAX_BOARD];
        int opp_count = generate_moves(gameData, opp_moves, opponent, 0, -1);

        for (int i = 0; i < opp_count && i < 10; i++) { 
            int idx = opp_moves[i].index;
            int fork_value = compute_fork_value(gameData, idx, opponent);
            
            if (fork_value > 0) {
                if (!is_double_three(gameData, idx, ia_player)) {
                    printf(">>> BLOCAGE PRÉVENTIF DE FOURCHETTE en (%d,%d)\n", 
                        GET_X(idx), GET_Y(idx));
                    best_move = idx;
                    goto play_move;
                }
            }
        }
    }

    // --- PHASE 2 : MINIMAX CLASSIQUE ---
    // (refresh_board_stats a déjà été fait au début, c'est bon)
    best_move = run_iterative_deepening(gameData, ia_player, start);

    if (best_move != -1 && is_double_three(gameData, best_move, ia_player)) {
        // Fallback coup illégal... (Code existant)
    }
    
    // ... (Code fallback existant) ...

    MoveCandidate emergency_moves[MAX_BOARD];
    int emergency_count = generate_moves(gameData, emergency_moves, ia_player, 0, -1);
    
    // Fallback si Minimax échoue ou retourne un coup illégal
    bool move_validated = false;
    if (best_move != -1 && !is_double_three(gameData, best_move, ia_player)) {
        move_validated = true;
    }

    if (!move_validated) {
        best_move = -1;
        for (int i = 0; i < emergency_count; i++) {
            if (!is_double_three(gameData, emergency_moves[i].index, ia_player)) {
                best_move = emergency_moves[i].index;
                break;
            }
        }
    }
    
    if (best_move == -1) {
        printf("    ❌ CATASTROPHE : Aucun coup légal trouvé !\n");
        for (int idx = 0; idx < MAX_BOARD; idx++) {
            if (gameData->board[idx] == EMPTY) {
                best_move = idx;
                break;
            }
        }
    }

play_move:
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}
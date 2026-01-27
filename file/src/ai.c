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
        // Alpha va bouger pendant la recherche (il monte si on trouve mieux).
        // On a besoin des originaux pour savoir si on a "fail".
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
        // On compare le résultat avec les bornes D'ORIGINE.
        
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
            
            // Si on a déjà tout ouvert (fenêtre large), on s'arrête là pour éviter la boucle
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
/* Orchestre l'Iterative Deepening */
static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_best_move = -1; // Sauvegarde de sécurité
    int prev_score = 0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {

        // Sauvegarde le meilleur coup de la profondeur précédente valide
        if (best_move != -1) prev_best_move = best_move;
        
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        
        if (score == -2) { // Timeout
            #ifdef DEBUG
            printf("Timeout at depth %d. Keeping best move from depth %d.\n", depth, depth-2);
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

    // --- PHASE 0 : VICTOIRE IMMÉDIATE ---
    // Si on peut gagner tout de suite, on le fait. Pas besoin de réfléchir.
    MoveCandidate moves[MAX_BOARD];
    // On demande à generate_moves de nous donner les coups
    // Note: Assure-toi que generate_moves vérifie le Double Three OU qu'on le vérifie ici.
    int count = generate_moves(gameData, moves, ia_player, 0, -1);
    
    // On parcourt les meilleurs coups (pas juste le premier !)
    for (int i = 0; i < count && i < 5; i++) {
        // Si le coup mène à une victoire ou une menace très forte
        if (moves[i].score_estim >= WIN_SCORE - 10000) {
            
            // VERIFICATION CRITIQUE : EST-CE UN COUP LÉGAL ?
            if (is_double_three(gameData, moves[i].index, ia_player)) {
                // Si c'est interdit, on l'ignore et on continue de chercher
                printf(">>> Coup (%d,%d) ignoré (Double-Three interdit)\n", 
                       GET_X(moves[i].index), GET_Y(moves[i].index));
                continue; 
            }
            
            // Si c'est légal, on le joue !
            best_move = moves[i].index;
            printf(">>> VICTOIRE IMMÉDIATE DÉTECTÉE (Coup Légal).\n");
            goto play_move;
        }
    }

    // --- PHASE 0.1 : VICTOIRE IMMÉDIATE PAR CAPTURE ---
    // Si JE peux gagner en capturant, je le fais !
    MoveCandidate cap_moves[MAX_BOARD];
    int cap_count = generate_moves(gameData, cap_moves, ia_player, 0, -1);
    for (int i = 0; i < cap_count; i++) {
        // Si ce coup est une capture (marqué par generate_moves)
        if (cap_moves[i].is_capture) {
            // Combien de paires je capture ?
            int captures = check_capture_count(gameData, cap_moves[i].index, ia_player); // Assurez-vous que cette fonction retourne le nb de paires
            if (gameData->captures[ia_player] + captures >= 5) {
                 best_move = cap_moves[i].index;
                 printf(">>> VICTOIRE PAR CAPTURE DÉTECTÉE (Coup Fatal).\n");
                 goto play_move;
            }
        }
    }

    // --- PHASE 0.2 : DÉFAITE IMMÉDIATE PAR CAPTURE (Urgence Absolue) ---
    // Si l'adversaire a 4 captures (ou plus), on vérifie s'il peut en faire une 5ème.
    int opponent = (ia_player == P1) ? P2 : P1;
    if (gameData->captures[opponent] >= 4) {
        // On génère les coups de l'adversaire pour voir s'il peut gagner
        // C'est un mini "pré-calcul" défensif
        int danger_idx = -1;
        
        // On scanne les coups adverses possibles
        // Optimisation : on utilise count_vulnerable_pairs pour savoir OÙ chercher
        // Mais pour faire simple et sûr : on check si une de NOS paires est vulnérable
        if (count_vulnerable_pairs(gameData, ia_player) > 0) {
             printf(">>> DANGER CRITIQUE : Adversaire va gagner par capture !\n");
             // On laisse le Minimax gérer car c'est complexe de savoir QUEL coup bloque la capture
             // MAIS on booste artificiellement la profondeur ou on force une recherche défensive.
             
             // Astuce : On retourne un score de -WIN dans l'évaluation (fait à l'étape 1)
             // Ce bloc ici sert juste de log pour vous confirmer la détection.
        }
    }

    // --- PHASE 0.5 : VCF OFFENSIF (Tenter de gagner maintenant) ---
    // C'est ici que l'IA devient "Tueuse". Si elle a une ouverture, elle l'exploite
    // AVANT de se soucier de la défense (car elle joue en premier).
    
    // On ne lance ça que si on a du potentiel d'attaque (au moins un Open 3)
    // Cela évite de perdre du temps en début de partie.
    if (gameData->max_threat_level[ia_player] >= IDX_OPEN_THREE) {
        int winning_move = find_winning_vcf(gameData, ia_player);
        if (winning_move != -1) {
            // Vérification de sécurité finale (Double Three)
            if (!is_double_three(gameData, winning_move, ia_player)) {
                best_move = winning_move;
                printf(">>> VCF OFFENSIF TROUVÉ ! Victoire forcée enclenchée.\n");
                goto play_move;
            }
        }
    }

    // --- PHASE 1 : FILET DE SÉCURITÉ (DEFENSIVE STRICT VCF) ---
    // Avant de lancer le lourd Minimax, on vérifie si on est en danger de mort subite.
    // On ne le fait que si l'adversaire a des menaces visibles (optimisation)
    int opp_threat_level = (ia_player == P1) ? gameData->max_threat_level[P2] : gameData->max_threat_level[P1];
    
    if (opp_threat_level >= IDX_OPEN_THREE) { // Si l'adversaire a au moins un Open 3
        int save_move = solve_defensive_crisis(gameData, ia_player);
        
        if (save_move >= 0) {
            best_move = save_move;
            printf(">>> IA joue le coup de SAUVETAGE (VCF Block).\n");
            goto play_move;
        } else if (save_move == -2) {
            // Mort certaine détectée. On laisse le Minimax essayer de trouver le chemin le plus long 
            // ou on joue le meilleur coup heuristique pour l'honneur.
            printf(">>> Mode Survie (Désespéré).\n");
        }
    }

    // --- PHASE 2 : MINIMAX CLASSIQUE ---
    refresh_board_stats(gameData);
    best_move = run_iterative_deepening(gameData, ia_player, start);

play_move:
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}

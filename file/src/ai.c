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
    int window = 5000;
    double time_budget = (double)TIME_LIMIT_MS / 1000.0;

    if (depth > 2 && abs(prev_score) >= 5000 && abs(prev_score) < WIN_SCORE - 10000) {
        window = 5000 + (abs(prev_score) / 100);
        alpha = prev_score - window;
        beta = prev_score + window;
        if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
        if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
    }

    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
    if (count == 0) return prev_score;
    if (*best_move_out == -1) *best_move_out = moves[0].index;
    int opponent_asp = (ia_player == P1) ? P2 : P1;

    // Fenêtre progressive : window ×1, ×4, pleine.
    // Objectif : éviter le re-run à fenêtre pleine (2× coût) quand la fenêtre élargie
    // suffit pour confirmer le score. Sur les positions stables (mid-game), la
    // fenêtre ×4 réussit dans >90% des cas → économise 100-150ms sur D10.
    // GARDE-TEMPS : avant tout retry, s'assurer qu'il reste au moins 55% du budget.
    // Sur fail à D10 (run 1 ≈60% budget), on accepte le résultat partiel : le coup
    // TT de D8 est en tête du tri → current_best_idx est presque toujours correct.
    int window_mult = 1;
    int loop_guard = 0;
    while (loop_guard++ < 3) {
        int alpha_origin = alpha;
        int beta_origin = beta;
        int current_best_idx = -1;
        int current_best_score = INT_MIN;
        bool time_out = false;

        for (int i = 0; i < count; i++) {
            int idx = moves[i].index;
            MoveUndo undo;
            apply_move(g, idx, ia_player, &undo);
            int val = -minimax(g, depth - 1, -beta, -alpha, false, opponent_asp, start);
            undo_move(g, ia_player, &undo);

            if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) { time_out = true; break; }
            if (val > current_best_score) {
                current_best_score = val;
                current_best_idx = idx;
            }
            if (val > alpha) alpha = val;
            if (alpha >= beta) { debug_cutoff_count++; break; }
        }

        if (time_out) return TIMEOUT_CODE;
        if (depth > 2 && (current_best_score <= alpha_origin || current_best_score >= beta_origin)) {
            // Vérification du budget avant retry.
            // Si >55% du budget écoulé au moment du fail, le retry coûterait plus
            // que le budget restant. On accepte le meilleur coup trouvé (TT move
            // de D-2 en tête = bonne qualité même avec fenêtre étroite).
            double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            if (elapsed > time_budget * 0.55) {
                if (current_best_idx != -1) *best_move_out = current_best_idx;
                return current_best_score;
            }
            // Fenêtre progressive : ×4 d'abord, puis pleine.
            window_mult = (window_mult == 1) ? 4 : 100;
            int new_window = window * window_mult;
            if (new_window > WIN_SCORE || window_mult >= 100) {
                alpha = -WIN_SCORE - 10000;
                beta  =  WIN_SCORE + 10000;
            } else {
                int center = (alpha_origin + beta_origin) / 2;
                alpha = center - new_window;
                beta  = center + new_window;
                if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
                if (beta  >  WIN_SCORE + 10000) beta  =  WIN_SCORE + 10000;
            }
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

    // Step=2 sur depths pairs : évite l'oscillation de parité (odd/even parity effect).
    // À depth impair, P2 joue en dernier → scores artificiellement gonflés → faux WIN_SCORE.
    // Même parité garantit que le fallback est toujours comparable au depth précédent.
    long long prev_nodes = 0;
    // OUVERTURE : quand le plateau est quasi-vide (< 4 pierres), l'arbre est trop
    // homogène pour que l'alpha-beta coupe. D8 coûte 0.40s pour le même résultat
    // que D4 (les scores sont plats, aucun pruning ne joue). On plafonne à D4.
    // Au-delà de 4 pierres, la TT est warm et le pruning reprend.
    int max_iter_depth = (g->stone_count < 4) ? 4 : MAX_DEPTH;
    for (int depth = 2; depth <= max_iter_depth; depth += 2) {
        if (best_move != -1) prev_best_move = best_move;
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        long long nodes_this_depth = debug_node_count - prev_nodes;
        prev_nodes = debug_node_count;
        if (score == TIMEOUT_CODE) return (prev_best_move != -1) ? prev_best_move : best_move;

        prev_score = score;
        printf("Depth %d complete. Score: %d | nodes: %lld (+%lld) | t: %.3fs\n",
               depth, score, debug_node_count, nodes_this_depth, elapsed);

        // ASPIRATION GUARD : si le score est quasi-terminal (±WIN_SCORE ± 5000),
        // ne pas l'utiliser comme centre de fenêtre pour la prochaine depth.
        // Un score quasi-terminal biaise la fenêtre aspiration → tous les coups
        // sont clampés au bord → depth suivante retourne le même biais en cascade.
        // On force prev_score=0 pour garantir une fenêtre ouverte.
        if (abs(score) >= WIN_SCORE - 5000) {
            prev_score = 0;
        }

        // Arret anticipe sur victoire/defaite reelle ou position quasi-déterminée.
        // Seuil : WIN_SCORE - 3000 (= 19997000) pour capturer les scores comme
        // ±19997999 qui indiquent une issue forcée à quelques coups.
        // Un seuil trop serré (ex: -1001) laissait ces scores passer → depths
        // supplémentaires inutiles → spike 0.5s+. Seuil aligné sur aspiration guard.
        // IMPORTANT : on n'accepte un quasi-terminal qu'à partir de depth 4.
        if (depth >= 4 && (score >= WIN_SCORE - 3000 || score <= -(WIN_SCORE - 3000))) break;
        // Laisser le timeout naturel gerer la limite de temps (90% du budget).
        if ((double)(clock() - start) / CLOCKS_PER_SEC > allocated_time * 0.90) break;
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
    // Priorite 0 : Defense forcee multi-menaces (avant VCF/minimax)
    // Réservé UNIQUEMENT au niveau 3 (2+ menaces simultanées = mort mathématique).
    // - Niveau 3 : l'adversaire a 2+ open fours ou 2+ coups gagnants → minimax ne peut
    //   pas choisir car toutes les branches semblent perdantes. L'heuristique cherche
    //   la case qui bloque le maximum de menaces en une fois.
    // - Niveau 2 (1 menace) : minimax avec beam=12 trouve le bloc lui-même (SORT_BLOCK_WIN
    //   en tête de l'ordering) et peut combiner défense + contre-attaque. On lui fait
    //   confiance plutôt que de court-circuiter avec une décision purement heuristique.
    if (!forcing_found && gameData->in_crisis && gameData->crisis_level >= 3) {
        int defense_move = find_best_defense_with_threat_space(gameData, ia_player);
        if (defense_move != -1 && !is_double_three(gameData, defense_move, ia_player)) {
            best_move = defense_move;
            forcing_found = true;
            reason = "Defense multi-menaces";
        }
    }

    // Priorite 1 : VCF (sequences de menaces forcees menant a la victoire)
    // Priorite 2 : Minimax avec approfondissement iteratif (evaluation complete)
    if (!forcing_found) {
        // VCF est purement offensif : il ne voit pas les victoires adverses.
        // Bloqué dans 3 cas :
        // 1. crisis_immediate_win : adversaire gagne en 1 coup (alignement ou 5e capture)
        // 2. captures[opponent] >= 3 : 2 paires de plus = victoire par capture. VCF
        //    expose souvent des paires → suicidaire. Minimax gèrera défense + attaque.
        // 3. crisis_level >= 2 : adversaire a un Open Four ou un coup gagnant. Même si
        //    le danger immédiat vient d'être bloqué le tour dernier, le niveau de crise
        //    actuel indique que l'adversaire reconstruit rapidement. VCF masquerait des
        //    tours défensifs critiques.
        bool vcf_blocked = gameData->crisis_immediate_win
                        || (gameData->captures[opponent] >= 3)
                        || (gameData->in_crisis && gameData->crisis_level >= 2);
        int vcf_move = !vcf_blocked ? find_winning_vcf(gameData, ia_player) : -1;
        if (vcf_move != -1 && !is_double_three(gameData, vcf_move, ia_player)) {
            best_move = vcf_move; reason = "VCF Gagnant";
        } else {
            best_move = run_iterative_deepening(gameData, ia_player, start);
            // Fallback uniquement si minimax n'a retourné aucun coup (timeout dès depth 2)
            // On ne jette JAMAIS un résultat minimax valide, même en position désespérée :
            // c'est précisément là que minimax est le plus utile.
            if (best_move == -1) {
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
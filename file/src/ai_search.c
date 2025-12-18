#include "../include/gomoku.h"
#include <limits.h>

int vcf_search(game *g, int depth, int player, int ia_player, clock_t start_time) {
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return 0;
    if (abs(evaluate_board(g, ia_player)) > WIN_SCORE / 2) return 1; 
    if (depth == 0) return 0; 

    int opponent = (player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, depth, -1);
    
    MoveCandidate vcf_moves[MAX_BOARD];
    int vcf_count = 0;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;
        if (player == ia_player) {
            g->board[idx] = player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), player) >= 5500) vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        } else {
            g->board[idx] = ia_player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), ia_player) >= 5500) vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        }
    }

    if (player == ia_player && vcf_count == 0) return 0;
    if (player != ia_player && vcf_count == 0) return 1;

    for (int i = 0; i < vcf_count; i++) {
        MoveUndo undo;
        apply_move(g, vcf_moves[i].index, player, &undo);
        int result = vcf_search(g, depth - 1, opponent, ia_player, start_time);
        undo_move(g, player, &undo);
        if (player == ia_player && result == 1) return 1; 
        if (player != ia_player && result == 0) return 0;
    }
    return (player != ia_player); 
}

int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    // Scan complet (-1) pour ne rater aucune victoire immédiate
    int count_ia = generate_moves(g, moves, ia_player, -1, -1);

    // 1. Victoire Immédiate (Mat en 1) - SEULE PRIORITÉ ABSOLUE ICI
    for (int i = 0; i < count_ia; i++) {
        int idx = moves[i].index;
        g->board[idx] = ia_player;
        
        // A. Victoire par Alignement
        if (get_point_score(g, GET_X(idx), GET_Y(idx), ia_player) >= WIN_SCORE) {
            g->board[idx] = EMPTY;
            return idx;
        }

        // B. Victoire par Capture (CORRECTIF 1)
        int cap_indices[10];
        int caps = apply_captures_for_ai(g, GET_X(idx), GET_Y(idx), ia_player, cap_indices);
        if (g->captures[ia_player] + (caps / 2) >= 5) {
            g->board[idx] = EMPTY;
            return idx; // GAGNE IMMÉDIATEMENT
        }

        g->board[idx] = EMPTY;
    }

    // 2. VCF Offensif (On garde ça, c'est fort pour finir la partie)
    // On ne lance le VCF que si on n'est pas en danger de mort immédiate (Open 4 adverse)
    // Pour savoir si on est en danger, on fait un check rapide.
    bool in_danger = false;
    for (int i = 0; i < count_ia; i++) {
        int idx = moves[i].index;
        g->board[idx] = opponent;
        if (get_point_score(g, GET_X(idx), GET_Y(idx), opponent) >= WIN_SCORE) {
            in_danger = true;
            g->board[idx] = EMPTY;
            break;
        }
        g->board[idx] = EMPTY;
    }

    if (!in_danger) { 
        for (int max_depth = 3; max_depth <= 15; max_depth += 2) {
            if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > (TIME_LIMIT_MS / 3)) break; 
            for (int i = 0; i < count_ia; i++) {
                int idx = moves[i].index;
                // Optimisation : On ne tente le VCF que sur les coups prometteurs
                if (moves[i].score_estim < 4000) continue;

                g->board[idx] = ia_player;
                int score = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
                g->board[idx] = EMPTY;
                
                if (score < 5500) continue; 

                MoveUndo undo;
                apply_move(g, idx, ia_player, &undo);
                int win = vcf_search(g, max_depth, opponent, ia_player, start_time);
                undo_move(g, ia_player, &undo);
                if (win) return idx;
            }
        }
    } 

    // SUPPRESSION DU BLOCAGE RÉFLEXE ET DU VCF DÉFENSIF ICI
    // Tout le reste est géré par Minimax pour assurer la profondeur.
    
    return -1; 
}

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    debug_node_count++;

    // TT Probe
    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { if (entry->value > alpha) alpha = entry->value; }
        else if (entry->flag == TT_UPPERBOUND) { if (entry->value < beta) beta = entry->value; }
        if (alpha >= beta) return entry->value;
    }
    
    if ((debug_node_count & 2047) == 0) {
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return -2;
    }

    int current_eval = evaluate_board(g, ia_player);
    
    // Si on a gagné ou perdu, on arrête
    if (abs(current_eval) > WIN_SCORE / 2) return current_eval;

    // --- QUIESCENCE SEARCH SIMPLIFIÉE (Extension) ---
    // Si depth == 0 mais qu'il y a une grosse menace, on continue un peu
    // (Implémentation basique : on ne s'arrête pas si on est en échec)
    // Pour l'instant, on garde le depth 0 strict pour la vitesse, mais on va filtrer les coups.

    if (depth <= 0) {
        return current_eval;
    }

    int current_player = maximizingPlayer ? ia_player : ((ia_player == P1) ? P2 : P1);
    int opponent = (current_player == P1) ? P2 : P1;

    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1; 
    
    // --- LOGIQUE DE COUPS FORCÉS (CRITIQUE POUR LA PROFONDEUR) ---
    // Avant de générer tous les coups, on vérifie si l'adversaire menace de gagner TOUT DE SUITE.
    // Si oui, on ne génère QUE les coups qui bloquent cette menace.
    
    int forced_moves_count = 0;
    int threat_idx = -1;
    
    // On scanne les coups de l'adversaire pour voir s'il a un Win ou Open 4
    // Note: C'est coûteux, mais nécessaire pour la stabilité.
    // Optimisation : On pourrait le déduire du score incrémental, mais le scan est plus sûr.
    
    // On cherche d'abord si NOUS pouvons gagner immédiatement (Move Killer ultime)
    MoveCandidate win_moves[MAX_BOARD];
    // Scan complet (-1) pour nous aussi
    int win_count = generate_moves(g, win_moves, current_player, -1, -1);
    for(int i=0; i<win_count; i++) {
        int idx = win_moves[i].index;
        g->board[idx] = current_player;
        if (get_point_score(g, GET_X(idx), GET_Y(idx), current_player) >= WIN_SCORE) {
            g->board[idx] = EMPTY;
            // On a trouvé un coup gagnant, on ne cherche pas plus loin, on le joue.
            // (Sauf si c'est un noeud All-Node, mais en Gomoku c'est souvent suffisant)
            tt_save(g->current_hash, depth, WIN_SCORE - (MAX_DEPTH - depth), TT_EXACT, idx);
            return WIN_SCORE - (MAX_DEPTH - depth);
        }
        // Check Capture Win pour nous aussi dans le minimax
        int cap_indices[10];
        int caps = apply_captures_for_ai(g, GET_X(idx), GET_Y(idx), current_player, cap_indices);
        if (g->captures[current_player] + (caps / 2) >= 5) {
             g->board[idx] = EMPTY;
             tt_save(g->current_hash, depth, WIN_SCORE - (MAX_DEPTH - depth), TT_EXACT, idx);
             return WIN_SCORE - (MAX_DEPTH - depth);
        }

        g->board[idx] = EMPTY;
    }

    // Ensuite, on regarde si l'ADVERSAIRE menace de gagner au prochain tour
    // Si oui, on doit bloquer.
    MoveCandidate opp_moves[MAX_BOARD];
    // CORRECTIF 2 : depth = -1 pour désactiver le Beam Search et voir TOUTES les menaces
    int opp_count = generate_moves(g, opp_moves, opponent, -1, -1);
    int must_block_indices[5]; // On peut avoir besoin de bloquer plusieurs endroits (ex: double 4)
    int must_block_count = 0;

    for(int i=0; i<opp_count; i++) {
        int idx = opp_moves[i].index;
        g->board[idx] = opponent;
        int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
        
        // Check Capture Win pour l'adversaire
        int cap_indices[10];
        int caps = apply_captures_for_ai(g, GET_X(idx), GET_Y(idx), opponent, cap_indices);
        bool capture_win = (g->captures[opponent] + (caps / 2) >= 5);

        g->board[idx] = EMPTY;

        if (score >= WIN_SCORE || capture_win) {
            // L'adversaire gagne ici. On DOIT jouer ici pour bloquer (ou capturer, mais bloquer est le standard)
            must_block_indices[must_block_count++] = idx;
            if (must_block_count >= 5) break;
        }
    }

    int move_count;
    
    if (must_block_count > 0) {
        // MODE SURVIE : On ne considère QUE les coups de blocage
        move_count = must_block_count;
        for(int i=0; i<must_block_count; i++) {
            moves[i].index = must_block_indices[i];
            moves[i].score_estim = 1000000; // Priorité max
        }
    } else {
        // MODE NORMAL
        move_count = generate_moves(g, moves, current_player, depth, tt_move);
    }

    if (move_count == 0) return 0;

    int best_val = maximizingPlayer ? INT_MIN : INT_MAX;
    int best_move_this_node = -1; 

    for (int i = 0; i < move_count; i++) {
        int idx = moves[i].index;

        // Check Double-Three (Règle standard)
        if (is_double_three(g, idx, current_player)) {
             // ... (Logique capture inchangée) ...
             // Pour simplifier ici, on assume que generate_moves ou la logique capture gère ça
             // Si c'est interdit, continue
             // (Code existant conservé pour la capture)
            int capture_indices[10];
            int caps = apply_captures_for_ai(g, GET_X(idx), GET_Y(idx), current_player, capture_indices);
            int opp = (current_player == P1) ? P2 : P1;
            for(int k=0; k<caps; k++) g->board[capture_indices[k]] = opp;
            if (caps == 0) continue; 
        }

        MoveUndo undo;
        apply_move(g, idx, current_player, &undo);
        
        int val;
        
        // LMR (Late Move Reduction)
        // On réduit moins agressivement car Gomoku est très tactique
        if (i == 0 || must_block_count > 0) { // Pas de réduction si on est forcé
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
        } else {
            int reduction = 0;
            if (depth >= 4 && i >= 4) reduction = 1;
            if (depth >= 6 && i >= 8) reduction = 2;
            
            // Recherche réduite
            if (maximizingPlayer) val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, !maximizingPlayer, ia_player, start_time);
            else val = minimax(g, depth - 1 - reduction, beta - 1, beta, !maximizingPlayer, ia_player, start_time);

            // Re-recherche si nécessaire
            if (reduction > 0 && ((maximizingPlayer && val > alpha) || (!maximizingPlayer && val < beta))) {
                 val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
            // Fenêtre complète si échec fenêtre nulle
            if ((maximizingPlayer && val > alpha && val < beta) || (!maximizingPlayer && val < beta && val > alpha)) {
                 val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
        }

        undo_move(g, current_player, &undo);
        if (val == -2) return -2; 

        if (maximizingPlayer) {
            if (val > best_val) { best_val = val; best_move_this_node = idx; }            
            if (val > alpha) alpha = val;
            if (beta <= alpha) {
                best_move_this_node = idx;
                if (killer_moves[depth][0] != idx) { killer_moves[depth][1] = killer_moves[depth][0]; killer_moves[depth][0] = idx; }
                history_heuristic[idx] += (depth * depth);
                debug_cutoff_count++;
                break;
            }
        } else {
            if (val < best_val) { best_val = val; best_move_this_node = idx; }
            if (val < beta) beta = val;
            if (beta <= alpha) {
                best_move_this_node = idx;
                if (killer_moves[depth][0] != idx) { killer_moves[depth][1] = killer_moves[depth][0]; killer_moves[depth][0] = idx; }
                history_heuristic[idx] += (depth * depth);
                debug_cutoff_count++;
                break; 
            }
        }
    }

    if (depth > 1) {
        int flag = (best_val <= alpha) ? TT_UPPERBOUND : (best_val >= beta ? TT_LOWERBOUND : TT_EXACT);
        tt_save(g->current_hash, depth, best_val, flag, best_move_this_node);
    }
    return best_val;
}
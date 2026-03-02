#include "../include/gomoku.h"
#include <limits.h>

/**
 * Verifie si le temps imparti est ecoule.
 * Permet d'interrompre la recherche avant le timeout du systeme.
 */
static inline bool is_timeout(clock_t start_time) {
    return ((double)(clock() - start_time) / CLOCKS_PER_SEC >= (double)TIME_LIMIT_MS / 1000.0);
}

/**
 * Recherche de quiescence : prolonge la recherche dans les positions tactiquement instables.
 * 
 * Evite l'effet d'horizon en evaluant les captures et menaces immediates au-dela de la profondeur limite.
 * N'explore que les coups "bruyants" (captures, menaces fortes) jusqu'a une position calme.
 * 
 * Retourne le score de la position, ou TIMEOUT_CODE si le temps est ecoule.
 */
static int quiescence_search(game *g, int alpha, int beta, int player, int qs_depth, clock_t start_time) {
    // Granularité 63 (au lieu de 127) : réduit la fenêtre max entre deux checks
    // de ~8ms à ~4ms, évite les dépassements de budget sur des nœuds lourds.
    if ((debug_node_count & 63) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // Stand Pat : evaluation statique de la position actuelle
    // Si cette evaluation est deja suffisante (cutoff beta), on peut arreter
    int stand_pat = evaluate_board(g, player);

    if (qs_depth <= 0) return stand_pat;
    
    if (stand_pat >= WIN_SCORE - 1000) return stand_pat;

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // Génération des coups (Captures et Menaces)
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, -1, -1);
    int opponent = (player == P1) ? P2 : P1;

    for (int i = 0; i < count; i++) {
        // AMÉLIORATION : Toujours explorer les captures + menaces sérieuses
        if (!moves[i].is_capture && moves[i].score_estim < CLOSED_THREE) continue;
        
        // Vérifier si on est proche de la mort par capture
        if (g->captures[opponent] >= 4 && !moves[i].is_capture) continue; // Focus captures

        int idx = moves[i].index;
        if (is_double_three(g, idx, player)) continue;

        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        if (moves[i].is_capture && evaluate_board(g, player) >= WIN_SCORE) {
            undo_move(g, player, &undo);
            return WIN_SCORE;
        }

        int score = -quiescence_search(g, -beta, -alpha, opponent, qs_depth - 1, start_time);

        undo_move(g, player, &undo);

        if (score == TIMEOUT_CODE || score == -TIMEOUT_CODE) return TIMEOUT_CODE;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

/**
 * Algorithme Negamax avec elagage alpha-beta et Principal Variation Search.
 * 
 * Variante simplifiee de Minimax exploitant la symetrie : max(a,b) = -min(-a,-b).
 * Optimisations :
 * - Table de transposition (evite de recalculer les positions deja vues)
 * - Elagage alpha-beta (coupe les branches inutiles)
 * - PVS (recherche optimiste avec fenetre nulle apres le premier coup)
 * - Quiescence search (prolonge la recherche dans les positions tactiques)
 * 
 * Retourne le score de la position du point de vue du joueur courant.
 */
int negamax(game *g, int depth, int alpha, int beta, int player, clock_t start_time, bool null_allowed) {
    debug_node_count++;

    // Verification du timeout (tous les 256 noeuds pour limiter l'overhead)
    // Granularité 63 : même raison que dans quiescence_search.
    // Avec beam=30, un seul niveau peut générer 900 nœuds sans check intermédiaire
    // si on utilise 255 → risque de dépasser 1.7s comme observé dans les logs.
    if ((debug_node_count & 63) == 0 && is_timeout(start_time)) return TIMEOUT_CODE;

    // Consultation de la table de transposition
    // Si cette position a deja ete evaluee a une profondeur suffisante, reutiliser le resultat
    int original_alpha = alpha;
    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { 
            if (entry->value > alpha) alpha = entry->value;
        }
        else if (entry->flag == TT_UPPERBOUND) { 
            if (entry->value < beta) beta = entry->value;
        }
        if (alpha >= beta) {
            debug_cutoff_count++;
            return entry->value;
        }
    }

    // Evaluation terminale : victoire/defaite ou profondeur limite atteinte
    // Si victoire detectee, retourner un score ajuste par la profondeur (favorise les victoires rapides)
    int current_eval = evaluate_board(g, player);
    if (abs(current_eval) >= WIN_SCORE - 1000) {
        return (current_eval > 0) ? (WIN_SCORE + depth) : (-WIN_SCORE - depth);
    }

    int opponent = (player == P1) ? P2 : P1;

    if (depth <= 0) {
        // QS depth adaptatif : 3 si des menaces actives existent, 2 sinon.
        // En position calme (pas de OPEN_THREE ni CLOSED_FOUR sur le plateau),
        // qs=3 génère ~8× plus de nœuds qu'qs=2 pour zéro gain de qualité.
        // qs=2 libère ~60% du budget QS en position calme → budget redistribué
        // vers des ply supplémentaires en minimax.
        // Guard : on garde qs=3 dès qu'un OPEN_THREE ou CLOSED_FOUR est présent
        // (position tactiquement instable) pour éviter l'effet d'horizon.
        int qs_depth = (g->threat_counts[opponent][IDX_OPEN_THREE] > 0
                        || g->threat_counts[player][IDX_OPEN_THREE] > 0
                        || g->max_threat_level[opponent] >= IDX_CLOSED_FOUR
                        || g->max_threat_level[player] >= IDX_CLOSED_FOUR) ? 3 : 2;
        return quiescence_search(g, alpha, beta, player, qs_depth, start_time);
    }

    // NULL MOVE PRUNING
    // Principe : on "passe" notre tour — si l'adversaire obtient quand même score < beta
    // après une recherche réduite, la position est si bonne pour nous qu'on peut couper.
    // Conditions de sécurité :
    //   - null_allowed : jamais deux null moves consécutifs (évite les faux cutoffs)
    //   - depth >= 3 : inutile en feuilles (overhead > gain)
    //   - !local_crisis : en défense critique AU NŒUD COURANT, passer serait catastrophique.
    //     On vérifie l'état local (pas g->in_crisis qui est figé à la racine) :
    //     si l'adversaire a un open_four ou une closed_four ici, on ne peut pas passer.
    //   - captures[opponent] < 4 : si adverse proche de gagner par capture, trop risqué
    //   - current_eval >= beta : on est déjà en position avantageuse (condition classique)
    //   - abs(current_eval) < WIN_SCORE - 10000 : pas dans une séquence de mat
    bool local_crisis = (g->max_threat_level[opponent] >= IDX_OPEN_FOUR
                         || g->captures[opponent] >= 4);
    if (null_allowed
        && depth >= 3
        && !local_crisis
        && g->captures[opponent] < 4
        && current_eval >= beta
        && abs(current_eval) < WIN_SCORE - 10000)
    {
        int R = (depth >= 6) ? 3 : 2;
        // Aucune pierre posée : on passe directement à l'adversaire
        int null_score = -negamax(g, depth - 1 - R, -beta, -beta + 1, opponent, start_time, false);
        if (null_score == TIMEOUT_CODE || null_score == -TIMEOUT_CODE) return TIMEOUT_CODE;
        if (null_score >= beta) {
            return beta; // Cutoff : même en passant, la position reste >= beta
        }
    }

    // 4. Génération
    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1;
    int count = generate_moves(g, moves, player, depth, tt_move);

    if (count == 0) return current_eval;

    int best_val = -WIN_SCORE - 20000;
    int best_move = moves[0].index;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;

        // FUTILITY PRUNING
        // Principe : si eval_statique + score_estimé_coup + marge <= alpha,
        // ce coup ne peut pas remonter le score jusqu'à alpha → inutile de l'explorer.
        // Les coups sont triés par score décroissant → dès que la condition est vraie,
        // TOUS les coups suivants échouent aussi → `break` au lieu de `continue`.
        //
        // Conditions de sécurité :
        //   - i > 0 : toujours explorer le 1er coup (PVS / meilleur coup TT)
        //   - !in_crisis : en défense, ne pas pruner des blocages potentiels
        //   - abs(eval) < WIN_SCORE - 50000 : pas dans une séquence forcée
        //   - score_estim < seuil : ne pas pruner les menaces sérieuses
        //
        // depth=1 : marge petite (CLOSED_THREE/2 = 25000), coups à partir de i=4
        //           qui n'atteignent pas CLOSED_THREE. Ces coups sont des remplissages
        //           positionnels sans valeur tactique immédiate.
        // depth=2 : marge plus grande (CLOSED_THREE = 50000) pour couvrir 2 coups,
        //           seulement les coups très tardifs (i >= 8) sous OPEN_TWO.
        if (!local_crisis && abs(current_eval) < WIN_SCORE - 50000) {
            if (depth == 1 && i >= 4
                && moves[i].score_estim < CLOSED_THREE
                && current_eval + moves[i].score_estim + (CLOSED_THREE / 2) <= alpha) {
                break;
            }
            if (depth == 2 && i >= 6
                && moves[i].score_estim < OPEN_TWO
                && current_eval + moves[i].score_estim + CLOSED_THREE <= alpha) {
                break;
            }
            // depth=3 : le niveau le plus peuplé de l'arbre D8 avec beam=8.
            // Marge = CLOSED_THREE*2 pour couvrir 3 coups potentiels (1 ply de
            // profondeur supplémentaire vs depth=2). Seuil i>=5 pour toujours
            // explorer les 5 premiers coups (TT+killers+menaces OPEN_THREE).
            if (depth == 3 && i >= 5
                && moves[i].score_estim < OPEN_TWO
                && current_eval + moves[i].score_estim + CLOSED_THREE * 2 <= alpha) {
                break;
            }
        }
        MoveUndo undo;
        apply_move(g, idx, player, &undo);

        int val;
        
        // PVS Logic
        if (i == 0) {
            val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time, true);
            // -TIMEOUT_CODE = +99999999 : intercepter la négation du code timeout
            if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) {
                undo_move(g, player, &undo);
                return TIMEOUT_CODE;
            }
        } else {
            // LMR
            // LMR : réduction des coups tardifs calmes pour libérer du budget.
            // R=1 depth>=3 i>=4 : coups calmes sans menace CLOSED_THREE.
            //       Avant : depth>=4 i>=6. Gain : ~30% nœuds en moins à depth 3-4.
            // R=2 depth>=3 i>=6 : coups vraiment calmes sous OPEN_TWO.
            //       Avant : depth>=4 i>=8. Couvre tous les coups positionnels.
            // R=3 depth>=5 i>=9 : coups sans valeur tangible (sous CLOSED_TWO)
            //       aux profondeurs élevées. Nouveau niveau pour D8→D10.
            // Guard commun : score_estim sert déjà de filtre tactique.
            // Si LMR produit un score > alpha, la recherche pleine confirme (ci-dessous).
            int R = 0;
            if (depth >= 3 && i >= 4 && moves[i].score_estim < CLOSED_THREE) R = 1;
            if (depth >= 3 && i >= 6 && moves[i].score_estim < OPEN_TWO)     R = 2;
            if (depth >= 5 && i >= 9 && moves[i].score_estim < CLOSED_TWO)   R = 3;
            
            val = -negamax(g, depth - 1 - R, -alpha - 1, -alpha, opponent, start_time, true);
            
            if (val == TIMEOUT_CODE) {
                 undo_move(g, player, &undo);
                 return TIMEOUT_CODE;
            }

            // LMR standard : si la recherche réduite bat alpha, on confirme
            // directement avec full-depth + full-window.
            // L'ancien code faisait : null(d-1-R) → null(d-1) → full(d-1) (3 recherches).
            // Ici :                    null(d-1-R) → full(d-1) (2 recherches).
            // La null-window intermédiaire à depth-1 était redondante :
            // si val > alpha au depth réduit, la seule information utile est
            // le score exact à depth plein — la null-window ne donne pas ce score.
            if (val > alpha && val < beta) {
                val = -negamax(g, depth - 1, -beta, -alpha, opponent, start_time, true);
                if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) {
                    undo_move(g, player, &undo);
                    return TIMEOUT_CODE;
                }
            }
        }
        
        undo_move(g, player, &undo);

        // Propagation Timeout (inclut -TIMEOUT_CODE issu de la négation)
        if (val == TIMEOUT_CODE || val == -TIMEOUT_CODE) return TIMEOUT_CODE;

        if (val > best_val) {
            best_val = val;
            best_move = idx;
        }

        if (val >= beta) {
            debug_cutoff_count++;
            // KILLER MOVES : mémoriser ce coup silencieux qui cause un cutoff.
            // Évite de réexplorer des coups non-capturants qui battent beta.
            // On ne met à jour que pour les coups sans capture pour ne pas
            // poluer la table avec des coups tactiques évidents.
            if (depth >= 0 && depth < MAX_DEPTH) {
                if (killer_moves[depth][0] != idx) {
                    killer_moves[depth][1] = killer_moves[depth][0];
                    killer_moves[depth][0] = idx;
                }
            }
            // HISTORY HEURISTIC : bonus proportionnel à la profondeur restante.
            // depth² favorise les cutoffs à haute profondeur (plus significatifs).
            if (idx >= 0 && idx < MAX_BOARD) {
                history_heuristic[idx] += depth * depth;
                if (history_heuristic[idx] > 20000) history_heuristic[idx] = 20000;
            }
            tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move);
            return val;
        }

        if (val > alpha) {
            alpha = val;
        }
    }

    // Sauvegarde TT
    if (best_val != TIMEOUT_CODE) {
        int flag = TT_UPPERBOUND;
        if (best_val > original_alpha) flag = TT_EXACT;
        tt_save(g->current_hash, depth, best_val, flag, best_move);
    }
    
    return best_val;
}

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    return negamax(g, depth, alpha, beta, ia_player, start_time, true);
}
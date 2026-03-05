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
        // Zone volatile [OPEN_THREE, OPEN_FOUR) : skip aspiration (scores instables entre depths)
        if (abs(prev_score) >= OPEN_THREE && abs(prev_score) < OPEN_FOUR) {
            // Full window directement
        } else {
            window = 5000 + (abs(prev_score) / 100);
            // Plancher pour scores forts
            if (abs(prev_score) >= OPEN_FOUR && window < OPEN_THREE / 4)
                window = OPEN_THREE / 4;
            alpha = prev_score - window;
            beta = prev_score + window;
            if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
            if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
        }
    }

    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
    if (count == 0) return prev_score;
    if (*best_move_out == -1) *best_move_out = moves[0].index;
    int opponent_asp = (ia_player == P1) ? P2 : P1;

    // Fenetre progressive : ×1, ×4, pleine. Retry si score hors fenetre.
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

            int val;
            if (i == 0) {
                // Premier coup : fenêtre pleine (candidat PV)
                val = -negamax(g, depth - 1, -beta, -alpha, opponent_asp, start, true);
            } else {
                // PVS : null-window d'abord, re-search si score entre alpha et beta
                val = -negamax(g, depth - 1, -alpha - 1, -alpha, opponent_asp, start, true);
                if (val != TIMEOUT_CODE && val != -TIMEOUT_CODE
                    && val > alpha && val < beta) {
                    val = -negamax(g, depth - 1, -beta, -alpha, opponent_asp, start, true);
                }
            }

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
            // Positions plates : fenetre plus large inutile
            if (abs(current_best_score) < CLOSED_THREE) {
                if (current_best_idx != -1) *best_move_out = current_best_idx;
                return current_best_score;
            }
            // Budget check : >60% consomme -> accepter le resultat partiel
            double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            if (elapsed > time_budget * 0.60) {
                if (current_best_idx != -1) *best_move_out = current_best_idx;
                return current_best_score;
            }
            // Elargir la fenetre : x4 puis pleine
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
 * Scan 1-ply : trouve le premier coup immédiatement gagnant parmi les candidats.
 *
 * Utilisé quand iterative deepening détecte un score quasi-terminal (≥WIN_SCORE-3000)
 * mais retournerait le meilleur coup à D4, qui ne correspond pas forcément au coup
 * qui exploite la victoire détectée.
 *
 * Exemple : D4 retourne (8,9) — bon pour les menaces latentes — mais (7,10) complète
 * un alignement immédiat que D4 ne voit pas comme "premier" dans son ordering.
 *
 * On fait un apply_move pour chaque candidat et on vérifie :
 *   - captures[player] >= 5 (victoire par capture)
 *   - threat_counts[player][IDX_WIN] > 0 (alignement de 5 immédiat)
 *
 * Retourne l'index du coup gagnant, ou -1 si aucun coup ne gagne immédiatement.
 */
static int find_immediate_win(game *g, int player) {
    for (int ci = 0; ci < g->cand_count; ci++) {
        int idx = g->cand_list[ci];
        if (is_double_three(g, idx, player)) continue;

        int x = GET_X(idx), y = GET_Y(idx);

        // VÉRIFICATION DIRECTE : ne pas utiliser threat_counts[IDX_WIN] qui peut
        // être un résidu d'une mise à jour incrémentale (false positive observé).
        // Pose temporaire de la pierre + scan directionnel direct via get_point_score_fast
        // (commence à stones=1 et lit les voisins depuis le plateau → correct).
        g->board[idx] = player;
        bool aligns = (get_point_score_fast(g, x, y, player) >= WIN_SCORE);
        g->board[idx] = EMPTY;

        if (aligns) return idx;

        // Victoire par capture : apply_move complet nécessaire pour compter les prises.
        MoveUndo undo;
        apply_move(g, idx, player, &undo);
        bool cap_win = (g->captures[player] >= 5);
        undo_move(g, player, &undo);
        if (cap_win) return idx;
    }
    return -1;
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

    // Step=2 sur depths paires pour eviter l'oscillation de parite
    long long prev_nodes = 0;
    // Ouverture : plateau quasi-vide -> max D4 (arbre trop homogene)
    int max_iter_depth = (g->stone_count < 2) ? 4 : MAX_DEPTH;
    double last_depth_duration = 0.0;  // durée du dernier depth complété
    double depth_start = 0.0;         // timestamp du début du depth courant
    for (int depth = 2; depth <= max_iter_depth; depth += 2) {
        // Garde-temps : >55% budget consomme -> stop
        if (depth > 2) {
            double pre_elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            if (pre_elapsed > allocated_time * 0.55) break;

            // Projection D(n+2) ~ 2.5x D(n) : skip si deborderait le budget
            if (last_depth_duration > 0.0) {
                double projected_end = pre_elapsed + last_depth_duration * 2.5;
                if (projected_end > allocated_time * 0.90) break;
            }
        }
        depth_start = (double)(clock() - start) / CLOCKS_PER_SEC;
        if (best_move != -1) prev_best_move = best_move;
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
        long long nodes_this_depth = debug_node_count - prev_nodes;
        prev_nodes = debug_node_count;
        if (score == TIMEOUT_CODE) return (prev_best_move != -1) ? prev_best_move : best_move;

        prev_score = score;
        ia_last_depth = depth;  // Track last completed depth
        last_depth_duration = elapsed - depth_start;
        printf("Depth %d complete. Score: %d | nodes: %lld (+%lld) | t: %.3fs\n",
               depth, score, debug_node_count, nodes_this_depth, elapsed);

        // Score quasi-terminal : reset fenetre aspiration pour eviter le biais
        if (abs(score) >= WIN_SCORE - 50000) {
            prev_score = 0;
        }

        // Arret anticipe sur victoire/defaite detectee
        if (depth >= 4 && score >= WIN_SCORE) {
            int imm = find_immediate_win(g, ia_player);
            if (imm != -1) {
                #ifdef DEBUG
                printf("[ID] win-override: (%d,%d)->(%d,%d) [1-ply]\n",
                       GET_X(best_move), GET_Y(best_move), GET_X(imm), GET_Y(imm));
                #endif
                best_move = imm;
            }
            break;
        }
        if (depth >= 4 && score >= WIN_SCORE - 1500) {
            int imm = find_immediate_win(g, ia_player);
            if (imm != -1) {
                #ifdef DEBUG
                printf("[ID] win-override dual: (%d,%d)->(%d,%d) [1-ply]\n",
                       GET_X(best_move), GET_Y(best_move), GET_X(imm), GET_Y(imm));
                #endif
                best_move = imm;
                break;  // victoire immédiate trouvée, inutile d'aller plus loin
            }
            // Pas de coup 1-ply gagnant : continuer la recherche pour trouver
            // le meilleur premier coup de la séquence duale (D6/D8/...).
        }
        // Quasi-win 2-ply : closed_four promotable, break sauf si adversaire a un reseau menacant
        if (depth >= 6 && score >= WIN_SCORE - 3000 && score < WIN_SCORE - 1001) {
            int my_cf = g->threat_counts[ia_player][IDX_CLOSED_FOUR];
            if (my_cf >= 1) {
                int opp      = (ia_player == P1) ? P2 : P1;
                int opp_of   = g->threat_counts[opp][IDX_OPEN_FOUR];
                int opp_ot   = g->threat_counts[opp][IDX_OPEN_THREE];
                int opp_ct   = g->threat_counts[opp][IDX_CLOSED_THREE];
                // Réseau dangereux : adversaire peut répondre par une menace simultanée
                bool opp_network = (opp_of >= 1)
                                   || (opp_ot >= 1 && opp_ct >= 2)
                                   || (opp_ot >= 2);
                if (!opp_network) {
                    #ifdef DEBUG
                    printf("[ID] win-override quasi-2ply: (%d,%d) [cf promote]\n",
                           GET_X(best_move), GET_Y(best_move));
                    #endif
                    break;  // best_move = premier coup de la promotion
                }
                // Adversaire a un reseau menacant : continuer la recherche
                #ifdef DEBUG
                printf("[ID] win-override suppressed (opp: of=%d ot=%d ct=%d)\n",
                       opp_of, opp_ot, opp_ct);
                #endif
            }
        }
        if (depth >= 4 && score <= -(WIN_SCORE - 3000)) break;
        // Laisser le timeout naturel gerer la limite de temps (90% du budget).
        if ((double)(clock() - start) / CLOCKS_PER_SEC > allocated_time * 0.90) break;
    }

    // Garde de securite : bloquer si adversaire gagne en 1 coup
    int opponent = (ia_player == 1) ? 2 : 1;
    int imm_opp = find_immediate_win(g, opponent);
    if (imm_opp != -1) {
        int imm_self = find_immediate_win(g, ia_player);
        if (imm_self == -1) {
            // Adversaire gagne en 1 coup : bloquer ou capturer
            if (!is_double_three(g, imm_opp, ia_player)) {
                #ifdef DEBUG
                printf("[AI] opp-win-guard: (%d,%d)->(%d,%d)\n",
                       GET_X(best_move), GET_Y(best_move), GET_X(imm_opp), GET_Y(imm_opp));
                #endif
                // Vérifier si bloquer résout vraiment (l'adversaire n'a pas un 2e coup gagnant)
                MoveUndo test_undo;
                apply_move(g, imm_opp, ia_player, &test_undo);
                int imm_opp2 = find_immediate_win(g, opponent);
                undo_move(g, ia_player, &test_undo);
                if (imm_opp2 != -1) {
                    // L'adversaire a encore un coup gagnant même après blocage → open four.
                    // Chercher un coup de capture qui casse l'alignement.
                    int cap_move = find_best_capture_move(g, ia_player);
                    if (cap_move != -1) {
                        // Vérifier que cette capture neutralise la menace.
                        apply_move(g, cap_move, ia_player, &test_undo);
                        int imm_after_cap = find_immediate_win(g, opponent);
                        undo_move(g, ia_player, &test_undo);
                        if (imm_after_cap == -1) {
                            #ifdef DEBUG
                            printf("[AI] capture (%d,%d) neutralizes open four\n",
                                   GET_X(cap_move), GET_Y(cap_move));
                            #endif
                            best_move = cap_move;
                        } else {
                            best_move = imm_opp; // Pas de meilleure option, bloquer un côté
                        }
                    } else {
                        best_move = imm_opp; // Pas de capture possible
                    }
                } else {
                    best_move = imm_opp; // Bloquer suffit (closed four, pas open four)
                }
            }
        }
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

    // Mise a jour du niveau de crise (informatif, ne force pas de coup)
    update_crisis_state(gameData, ia_player);

    // Opening book : coups predefinis quand le plateau est quasi-vide
    if (gameData->stone_count <= 1) {
        int book_idx = -1;
        int center = GET_INDEX(9, 9);
        if (gameData->stone_count == 0) {
            book_idx = center;
        } else { // stone_count == 1
            if (gameData->board[center] == EMPTY) {
                book_idx = center;
            } else {
                // Humain a joué au centre : répondre sur un point adjacent non-double-three
                int adj[] = { GET_INDEX(9,8), GET_INDEX(10,9), GET_INDEX(8,9), GET_INDEX(9,10),
                              GET_INDEX(10,8), GET_INDEX(10,10), GET_INDEX(8,8), GET_INDEX(8,10) };
                for (int j = 0; j < 8; j++) {
                    if (gameData->board[adj[j]] == EMPTY
                        && !is_double_three(gameData, adj[j], ia_player)) {
                        book_idx = adj[j];
                        break;
                    }
                }
            }
        }
        if (book_idx != -1) {
            best_move = book_idx;
            forcing_found = true;
            reason = "Opening Book";
        }
    }

    // Phase 1 : generation et tri des coups candidats
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(gameData, moves, ia_player, 0, -1);
    
    if (count == 0) {
        // Plateau vide impossible en milieu de partie, mais par sécurité
        best_move = GET_INDEX(9, 9);
        forcing_found = true;
        reason = "Centre (plateau vide)";
    } else {
        // Forcer uniquement victoire/mort immediate
        if (moves[0].score_estim >= SORT_WIN_IMMEDIATE) {
            best_move = moves[0].index;
            forcing_found = true;
            
            // Determiner si victoire ou blocage
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

    // Phase 2 : recherche strategique
    // Defense precomputee comme filet de securite, minimax decide du meilleur coup
    bool defense_needed = gameData->in_crisis &&
        (gameData->crisis_level >= 2 ||
         (gameData->crisis_level >= 1 && gameData->crisis_immediate_win));
    int precomputed_defense = -1;
    if (!forcing_found && defense_needed) {
        precomputed_defense = find_best_defense_with_threat_space(gameData, ia_player);
    }

    // VCF puis minimax
    if (!forcing_found) {
        // VCF bloque si adversaire gagne en 1, captures >= 3, ou closed_four adverse
        bool vcf_blocked = gameData->crisis_immediate_win
                        || (gameData->captures[opponent] >= 3)
                        || (gameData->threat_counts[opponent][IDX_CLOSED_FOUR] >= 1);
        int vcf_move = !vcf_blocked ? find_winning_vcf(gameData, ia_player) : -1;
        if (vcf_move != -1 && !is_double_three(gameData, vcf_move, ia_player)) {
            best_move = vcf_move; reason = "VCF Gagnant";
        } else {
            best_move = run_iterative_deepening(gameData, ia_player, start);

            // Recours defensif : substituer si minimax laisse une position perdante
            if (best_move != -1 && defense_needed) {
                MoveUndo rescue_undo;
                apply_move(gameData, best_move, ia_player, &rescue_undo);
                int mm_score = evaluate_board(gameData, ia_player);
                undo_move(gameData, ia_player, &rescue_undo);
            // Déclencher le recours si score quasi-terminal perdant
                bool rescue_needed = (mm_score <= -(WIN_SCORE - 3000))
                                  || (gameData->crisis_level >= 2 && mm_score <= -(WIN_SCORE - 50000));
                if (rescue_needed && precomputed_defense != -1
                    && !is_double_three(gameData, precomputed_defense, ia_player)) {
                    #ifdef DEBUG
                    printf("[AI] defense-rescue: mm(%d,%d) s=%d -> def(%d,%d)\n",
                           GET_X(best_move), GET_Y(best_move), mm_score,
                           GET_X(precomputed_defense), GET_Y(precomputed_defense));
                    #endif
                    best_move = precomputed_defense;
                    reason = "Defense recours";
                }
            }

            // Fallback uniquement si minimax n'a retourné aucun coup (timeout dès depth 2)
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

    // Log final après validation (un seul printf, évite le doublon P10)

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

    printf("[AI] (%d,%d) %s | D%d | %.3fs\n", GET_X(best_move), GET_Y(best_move), reason, ia_last_depth, time_spent);
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}
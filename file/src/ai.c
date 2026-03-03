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
        // ZONE VOLATILE [OPEN_THREE, OPEN_FOUR) = [2M, 10M) : positions tactiques avec
        // menaces non résolues. Le score peut changer de ±9M entre D(n) et D(n+2)
        // (ex: D6=+8.9M → D8=-94K observé). L'aspiration centrée sur 8.9M force
        // 2 retries coûteux (×4 window puis full) qui triplent le budget de D8.
        // Solution : skip aspiration dans cette zone → 1 seule recherche full window.
        // Les quasi-terminaux (≥OPEN_FOUR=10M) bénéficient toujours de l'aspiration
        // car ils sont stables (quasi-terminaux = peu de variation entre profondeurs).
        if (abs(prev_score) >= OPEN_THREE && abs(prev_score) < OPEN_FOUR) {
            // Full window : pas de restriction, pas de retry
            // alpha/beta restent à leurs valeurs larges par défaut
        } else {
            window = 5000 + (abs(prev_score) / 100);
            // Plancher : OPEN_THREE/4 = 500k pour les scores forts (≥OPEN_FOUR).
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

            int val;
            if (i == 0) {
                // Premier coup : fenêtre pleine (candidat PV)
                val = -negamax(g, depth - 1, -beta, -alpha, opponent_asp, start, true);
            } else {
                // PVS : null-window d'abord (beaucoup moins coûteuse)
                // Si le score dépasse alpha sans atteindre beta → on re-cherche en plein
                // Gain : moves qui échouent alpha (cas fréquent avec bon ordering) coûtent
                // ~2-3× moins cher qu'une recherche pleine.
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
            // Pas de retry si la position est plate (score < CLOSED_THREE) :
            // une fenêtre plus large ne changera pas l'évaluation d'une position
            // sans menace réelle. Économise ~200ms sur T3 où score ≈ ±4000.
            if (abs(current_best_score) < CLOSED_THREE) {
                if (current_best_idx != -1) *best_move_out = current_best_idx;
                return current_best_score;
            }
            // Vérification du budget avant retry.
            // Si >55% du budget écoulé au moment du fail, le retry coûterait plus
            // que le budget restant. On accepte le meilleur coup trouvé (TT move
            // de D-2 en tête = bonne qualité même avec fenêtre étroite).
            double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            if (elapsed > time_budget * 0.60) {
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

    // Step=2 sur depths pairs : évite l'oscillation de parité (odd/even parity effect).
    // À depth impair, P2 joue en dernier → scores artificiellement gonflés → faux WIN_SCORE.
    // Même parité garantit que le fallback est toujours comparable au depth précédent.
    long long prev_nodes = 0;
    // OUVERTURE : quand le plateau est quasi-vide (< 2 pierres = seulement le 1er coup IA),
    // l'arbre est trop homogène pour que l'alpha-beta coupe. On plafonne à D4 uniquement
    // pour le tout premier coup joué (stone_count==0 ou 1 : pas de contexte tactique).
    // Dès la 2e pierre, la TT est warm et le pruning reprend, D10+ est accessible.
    int max_iter_depth = (g->stone_count < 2) ? 4 : MAX_DEPTH;
    double last_depth_duration = 0.0;  // durée du dernier depth complété
    double depth_start = 0.0;         // timestamp du début du depth courant
    for (int depth = 2; depth <= max_iter_depth; depth += 2) {
        // Pré-check 1 — garde absolue : >55% du budget consommé → stop.
        // Couvre le cas D(n) terminé à ~61% du budget.
        if (depth > 2) {
            double pre_elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            if (pre_elapsed > allocated_time * 0.55) break;

            // Pré-check 2 — projection D(n+2) ≈ 2.5× D(n).
            // Si D(n) a pris `last_depth_duration` secondes et qu'on estime que
            // D(n+2) en prendra ~2.5× autant, on vérifie que le total projeté
            // resterait dans le budget avant de lancer.
            // Exemple : D8 à 131ms (33%), last_depth_duration=131ms.
            // Projection D10 ≈ 328ms. Total projeté : 131 + 328 = 459ms > 360ms → stop.
            // Facteur 2.5 empirique (branching factor typique mid-game avec pruning).
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

        // ASPIRATION GUARD : si le score est quasi-terminal (±WIN_SCORE ± 50000),
        // ne pas l'utiliser comme centre de fenêtre pour la prochaine depth.
        // Un score quasi-terminal biaise la fenêtre aspiration → tous les coups
        // sont clampés au bord → depth suivante retourne le même biais en cascade.
        // Seuil WIN_SCORE-50000 couvre WIN_SCORE±depth ET les quasi-terminaux
        // d'evaluate_board (WIN_SCORE-1001 à WIN_SCORE-3501).
        // On force prev_score=0 pour garantir une fenêtre ouverte.
        if (abs(score) >= WIN_SCORE - 50000) {
            prev_score = 0;
        }

        // Arret anticipe sur victoire/defaite reelle ou position quasi-déterminée.
        // Deux cas distincts :
        // 1) score >= WIN_SCORE (vrai terminal : alignement ou 5 captures) → break
        //    immédiat, le coup best_move est celui qui ferme la ligne / capte.
        // 2) score >= WIN_SCORE-1500 mais < WIN_SCORE (= WIN_SCORE-1001,
        //    ≥2 open fours) → on cherche un coup de clôture 1-ply.
        //    - Si trouvé : WIN-OVERRIDE + break (la victoire est à 1 ply).
        //    - Si NON trouvé : on NE break PAS. La victoire est en 2+ plies
        //      (jouer un open four, bloquer, puis l'autre). Il faut continuer
        //      D6/D8 pour que le PV identifie le BON premier coup de la séquence.
        //      Briser ici avec le coup D4 causait la répétition "19998999 × N
        //      sans conversion" vue en G4.
        if (depth >= 4 && score >= WIN_SCORE) {
            int imm = find_immediate_win(g, ia_player);
            if (imm != -1) {
                #ifdef DEBUG
                printf(">>> WIN-OVERRIDE : (%d,%d) → (%d,%d) [1-ply scan]\n",
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
                printf(">>> WIN-OVERRIDE (dual) : (%d,%d) → (%d,%d) [1-ply scan]\n",
                       GET_X(best_move), GET_Y(best_move), GET_X(imm), GET_Y(imm));
                #endif
                best_move = imm;
                break;  // victoire immédiate trouvée, inutile d'aller plus loin
            }
            // Pas de coup 1-ply gagnant : continuer la recherche pour trouver
            // le meilleur premier coup de la séquence duale (D6/D8/...).
        }
        // QUASI-WIN 2-PLY : score ∈ [WIN_SCORE-3000, WIN_SCORE-1501]
        // = double closed_four (WIN_SCORE-2001) ou closed_four+open_three (WIN_SCORE-2500)
        // Ces quasi-terminaux = victoire en 2 plies : AI joue pour promouvoir
        // un closed_four en open_four, puis gagne. find_immediate_win (1-ply) échoue.
        // Sans break, on continue à D(n+2) — mais si l'adversaire a du temps de
        // réponse entre les profondeurs, il peut construire sa propre menace et faire
        // osciller de +WIN_SCORE à -WIN_SCORE observé dans les logs.
        // Solution : si on a ≥1 closed_four et score quasi-win, garder best_move
        // et breaker immédiatement — SAUF si l'adversaire construit un réseau de
        // forks dangereux simultanément (opp_open_fours >= 1, ou
        // opp_open_threes >= 1 + opp_closed_threes >= 2 = conversion imminente).
        // Dans ce cas, continuer la recherche pour que negamax voie la menace adverse
        // et choisisse entre promouvoir offensivement ou bloquer d'abord.
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
                    printf(">>> WIN-OVERRIDE (quasi-2ply) : (%d,%d) [closed_four promote]\n",
                           GET_X(best_move), GET_Y(best_move));
                    #endif
                    break;  // best_move = premier coup de la promotion
                }
                // opp_network=true : ne pas breaker, laisser negamax peser
                // l'offensive vs la défense aux profondeurs suivantes.
                #ifdef DEBUG
                printf(">>> WIN-OVERRIDE SUPPRESSED (opp_network : of=%d ot=%d ct=%d) : continuer recherche\n",
                       opp_of, opp_ot, opp_ct);
                #endif
            }
        }
        if (depth >= 4 && score <= -(WIN_SCORE - 3000)) break;
        // Laisser le timeout naturel gerer la limite de temps (90% du budget).
        if ((double)(clock() - start) / CLOCKS_PER_SEC > allocated_time * 0.90) break;
    }

    // GARDE DE SÉCURITÉ FINALE : si l'adversaire peut gagner en 1 coup, on doit
    // le bloquer — sauf si on a soi-même une victoire immédiate 1-ply.
    // On utilise find_immediate_win(opponent) plutôt que threat_counts[IDX_OPEN_FOUR]
    // car threat_counts peut être désynchronisé de la détection CRISE NIVEAU 2.
    int opponent = (ia_player == 1) ? 2 : 1;
    int imm_opp = find_immediate_win(g, opponent);
    if (imm_opp != -1) {
        int imm_self = find_immediate_win(g, ia_player);
        if (imm_self == -1) {
            // L'adversaire gagne en 1 coup et on ne gagne pas en 1 coup.
            // Bloquer, SAUF si ce coup crée un double-three pour nous (illégal).
            if (!is_double_three(g, imm_opp, ia_player)) {
                #ifdef DEBUG
                printf(">>> OPP-WIN-GUARD : (%d,%d) → (%d,%d) [block opp immediate win]\n",
                       GET_X(best_move), GET_Y(best_move), GET_X(imm_opp), GET_Y(imm_opp));
                #endif
                best_move = imm_opp;
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

    // Les statistiques du plateau (pos_score, threat_counts) sont maintenues
    // incrémentalement par apply_move/undo_move — pas besoin de refresh_board_stats ici.
    // Calcul du niveau de crise (informatif uniquement, ne force plus les coups)
    update_crisis_state(gameData, ia_player);

    // OPENING BOOK : évite le calcul inutile sur un plateau quasi-vide.
    // stone_count == 0 : IA P1, joue au centre.
    // stone_count == 1 : IA P2, répond au premier coup humain.
    // Économie : ~5ms × 2 coups = budget redistribuable sur les plies critiques.
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
    // Politique révisée — minimax n'est PLUS court-circuité pendant les crises.
    //
    // Ancien problème : defense_needed → forcing_found=true → minimax jamais exécuté
    // → l'IA jouait en pure défense répétitive → perdait contre un joueur agressif
    // qui créait une menace Open Four à chaque tour.
    //
    // Nouvelle politique :
    //   1. Pré-calculer le coup défensif (fallback) via find_best_defense
    //   2. Laisser minimax chercher le meilleur coup (attaque + défense intégrées)
    //   3. Après minimax, valider que le coup choisi résout la crise
    //   4. Si minimax échoue, substituer par le coup défensif pré-calculé
    //
    // Minimax bloque naturellement les Open Fours : ne pas bloquer = -WIN_SCORE
    // à depth 1 (l'adversaire joue Five). C'est corrigé automatiquement par la
    // recherche, sans besoin de forçage explicite.
    bool defense_needed = gameData->in_crisis &&
        (gameData->crisis_level >= 2 ||
         (gameData->crisis_level >= 1 && gameData->crisis_immediate_win));
    int precomputed_defense = -1;
    if (!forcing_found && defense_needed) {
        precomputed_defense = find_best_defense_with_threat_space(gameData, ia_player);
        // NOTE: Ne PAS setter forcing_found — minimax doit chercher des contre-attaques.
        // precomputed_defense sert de filet de sécurité post-minimax.
    }

    // Priorite 1 : VCF (sequences de menaces forcees menant a la victoire)
    // Priorite 2 : Minimax avec approfondissement iteratif (evaluation complete)
    if (!forcing_found) {
        // VCF est purement offensif : il ne voit pas les victoires adverses.
        // Bloqué dans 3 cas :
        // 1. crisis_immediate_win : adversaire gagne en 1 coup (alignement ou 5e capture)
        // 2. captures[opponent] >= 3 : 2 paires de plus = victoire par capture. VCF
        //    expose souvent des paires → suicidaire. Minimax gèrera défense + attaque.
        // 3. opponent a ≥1 closed four : à 1 coup de créer un open four. VCF ignore
        //    ce danger et pourrait jouer offensif pendant que l'adversaire promeut.
        // Note : crisis_level >= 2 ne bloque PLUS VCF. VCF gère les positions où
        // l'adversaire a un Open Four via son soundness check (defender_survives).
        // Si VCF trouve un gain forcé, l'adversaire ne peut pas le stopper (même
        // avec son Open Four, car VCF simule toutes les réponses défensives).
        bool vcf_blocked = gameData->crisis_immediate_win
                        || (gameData->captures[opponent] >= 3)
                        || (gameData->threat_counts[opponent][IDX_CLOSED_FOUR] >= 1);
        int vcf_move = !vcf_blocked ? find_winning_vcf(gameData, ia_player) : -1;
        if (vcf_move != -1 && !is_double_three(gameData, vcf_move, ia_player)) {
            best_move = vcf_move; reason = "VCF Gagnant";
        } else {
            best_move = run_iterative_deepening(gameData, ia_player, start);

            // RECOURS DÉFENSIF post-minimax : valider que minimax résout la crise.
            // Si minimax choisit un coup qui laisse l'adversaire en position gagnante,
            // substituer par le coup défensif pré-calculé.
            if (best_move != -1 && defense_needed) {
                MoveUndo rescue_undo;
                apply_move(gameData, best_move, ia_player, &rescue_undo);
                int mm_score = evaluate_board(gameData, ia_player);
                undo_move(gameData, ia_player, &rescue_undo);
                // Déclencher sur :
                //   a) Score très négatif (quasi-terminal perdant)
                //   b) crisis_level >= 2 ET score négatif significatif
                bool rescue_needed = (mm_score <= -(WIN_SCORE - 3000))
                                  || (gameData->crisis_level >= 2 && mm_score <= -(WIN_SCORE - 50000));
                if (rescue_needed && precomputed_defense != -1
                    && !is_double_three(gameData, precomputed_defense, ia_player)) {
                    #ifdef DEBUG
                    printf(">>> DEFENSE-RESCUE : minimax (%d,%d) score=%d → defense (%d,%d)\n",
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

    printf("✅ [%s] (%d, %d). Raison: %s [Temps: %.3fs]\n", forcing_found ? "FORCED" : "AI", GET_X(best_move), GET_Y(best_move), reason, time_spent);
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}
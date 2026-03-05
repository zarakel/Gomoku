#include "../include/gomoku.h"
#include <stdlib.h>

/**
 * Fonction de comparaison pour qsort.
 * Trie les coups par score decroissant (meilleurs coups en premier).
 */
static int compare_moves(const void *a, const void *b) {
    MoveCandidate *ma = (MoveCandidate *)a;
    MoveCandidate *mb = (MoveCandidate *)b;
    return (mb->score_estim - ma->score_estim);
}

/**
 * Verifie si un coup adverse pourrait capturer nos pieces.
 * Simule temporairement le coup de l'adversaire pour detecter les captures potentielles.
 */
static bool would_capture_us(game *g, int idx, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx), y = GET_Y(idx);
    
    // On simule le coup de l'adversaire
    g->board[idx] = opponent;
    int caps = count_potential_captures(g, x, y, opponent);
    g->board[idx] = EMPTY;
    
    return (caps > 0);
}

/**
 * Calcule le score de tri d'un coup pour l'ordonnancement.
 * 
 * Hierarchie de priorites (du plus important au moins important) :
 * 0. Coups de la table de transposition (deja evalues comme meilleurs)
 * 1. Victoires immediates (alignement de 5 ou 5eme capture)
 * 2. Blocages de victoires adverses
 * 3. Menaces offensives (OPEN_FOUR, fourchettes)
 * 4. Protection anti-capture
 * 5. Captures tactiques
 * 6. Defense preventive
 * 
 * Strategie : Privilegie l'offensive (x3.0) sur la defense (x0.5).
 */
static int score_move_ordering(game *g, int idx, int player, int tt_move, int depth, int pre_my_caps) {
    if (idx == tt_move) return SORT_HASH;

    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx), y = GET_Y(idx);
    
    // my_caps pré-calculé par generate_moves pour éviter le double appel.
    int my_caps = pre_my_caps;
    // would_capture_us : actif a depth >= 5, ou inconditionnel si captures[opp] >= 4
    bool enemy_can_cap;
    if (g->captures[opponent] >= 4) {
        enemy_can_cap = would_capture_us(g, idx, player);
    } else {
        enemy_can_cap = (depth >= 5) ? would_capture_us(g, idx, player) : false;
    }
    
    // Evaluation menaces : fast aux noeuds internes, complet pres de la racine
    int atk_score, def_score;
    if (depth <= 6) {
        atk_score = get_point_score_fast(g, x, y, player);
        def_score = get_point_score_fast(g, x, y, opponent);
    } else {
        atk_score = get_point_score(g, x, y, player);
        def_score = get_point_score(g, x, y, opponent);
    }

    // --- HIÉRARCHIE DE TRI ULTRA-OFFENSIVE ---
    
    // Niveau 0 : Victoire immédiate
    if (atk_score >= WIN_SCORE) return SORT_WIN_IMMEDIATE;
    if (g->captures[player] + (my_caps/2) >= 5) return SORT_WIN_IMMEDIATE;

    // Niveau 1 : Survie CRITIQUE UNIQUEMENT (bloquer victoire adverse)
    if (def_score >= WIN_SCORE) return SORT_BLOCK_WIN + 10000000;
    if (g->captures[opponent] >= 4 && enemy_can_cap) return SORT_BLOCK_WIN + 5000000;

    // Niveau 2 : OFFENSIVE MAXIMALE - Créer menaces AVANT de défendre
    // OPEN_FOUR offensif : penaliser si expose des paires capturable en fin de partie
    if (atk_score >= OPEN_FOUR) {
        if (g->captures[opponent] >= 3) {
            int vuln = count_vulnerable_pairs_after_move(g, idx, player);
            if (vuln > 0) return -(OPEN_FOUR / 2) * vuln;
        }
        return SORT_THREAT_MAX + 5000000; // Boost massif
    }
    
    // Blocage fourchette adverse : depth guard adaptatif selon menaces existantes
    int opp_fork_value = 0;
    bool early_multi_threat = (g->threat_counts[opponent][IDX_OPEN_TWO] >= 2
                               || g->threat_counts[opponent][IDX_CLOSED_THREE] >= 1);
    int fork_depth_guard = early_multi_threat ? 4 : 6;
    bool global_fork_threat = (depth >= fork_depth_guard) && (g->threat_counts[opponent][IDX_OPEN_TWO] >= 2);
    if (depth >= fork_depth_guard && (def_score >= CLOSED_THREE || global_fork_threat)) {
        opp_fork_value = compute_fork_value(g, idx, opponent);
    }
    if (opp_fork_value > 0) {
        int fork_bonus = (g->threat_counts[opponent][IDX_OPEN_THREE] >= 1) ? 2000000 : 1000000;
        // Reseau multi-fork : priorite absolue
        if (g->threat_counts[opponent][IDX_OPEN_THREE] >= 1
            && g->threat_counts[opponent][IDX_CLOSED_THREE] >= 2)
            fork_bonus = 3500000;  // Plus urgent que combo OPEN_THREE seul (2M)
        return SORT_THREAT_MAX + fork_bonus;
    }

    // Niveau 2.5b : DOUBLE-FORK OFFENSIF — guard symétrique.
    int fork_value = 0;
    int my_fork_depth_guard = (g->threat_counts[player][IDX_OPEN_TWO] >= 2) ? 4 : 6;
    if (depth >= my_fork_depth_guard && atk_score >= CLOSED_THREE) {
        fork_value = compute_fork_value(g, idx, player);
    }
    if (fork_value > 0) {
        return SORT_THREAT_MAX + 3000000;
    }
    
    // OPEN_THREE offensif
    if (atk_score >= OPEN_THREE) return SORT_THREAT_MAX;

    // Dual-purpose : bloque menace adverse + cree menace offensive
    if (def_score >= OPEN_THREE && atk_score >= CLOSED_THREE) {
        int dual_bonus = 12000000 + (atk_score / 10);
        return dual_bonus;
    }

    // Blocage CLOSED_FOUR adverse : priorite entre OPEN_THREE offensif et dual-purpose
    if (def_score >= CLOSED_FOUR && def_score < OPEN_FOUR) return 25000000 + (atk_score / 100);

    // Niveau 3 : Défense SECONDARY (après avoir testé offensive)
    if (def_score >= OPEN_FOUR) return SORT_BLOCK_WIN - 1000000; // Réduit priorité
    
    // Niveau 4 : protection anti-capture (depth >= 5)
    int vuln_count = (depth >= 5) ? count_vulnerable_pairs_after_move(g, idx, player) : 0;
    if (vuln_count > 0) {
        int vulnerability_malus = vuln_count * 5000000;
        if (g->captures[opponent] >= 3)
            vulnerability_malus *= 5;
        int penalty = vulnerability_malus;
        if (penalty > atk_score + def_score) {
            return -penalty;
        }
    }
    
    // Niveau 5 : Capture tactique
    int capture_priority = 0;
    if (my_caps > 0) {
        capture_priority = SORT_CAPTURE + (my_caps * 100000);
        if (def_score > 0) capture_priority += 500000; 
    }

    // Niveau 6 : Défense préventive (basse priorité)
    int defense_priority = 0;
    if (enemy_can_cap) {
        defense_priority = SORT_BLOCK_WIN / 4; // Réduit de moitié
    }

    // Score final : offensive x3, defense x1.2
    double att_weight = 3.0;
    double def_weight = 1.2;
    int final_score = (int)(atk_score * att_weight) + (int)(def_score * def_weight) + capture_priority + defense_priority;
    
    // BONUS pour coups offensifs (même faibles)
    if (atk_score >= CLOSED_THREE) {
        final_score = (int)(final_score * 1.5); // +50% pour offensive
    }

    // Bonus de centralité et heuristiques de recherche
    final_score += (20 - (abs(x - 9) + abs(y - 9))) * 100;

    // Bonus connexion O(1) via cand_refcount
    if (final_score < OPEN_THREE) {
        int rc = g->cand_refcount[idx];
        if (rc >= 2) final_score += rc * 600;  // ~1200 pour rc=2, ~6000 pour rc=10
    }

    if (depth >= 0 && depth < MAX_DEPTH) {
        if (idx == killer_moves[depth][0]) final_score += SORT_KILLER_1;
        if (idx == killer_moves[depth][1]) final_score += SORT_KILLER_2;
    }
    final_score += history_heuristic[idx];

    return final_score;
}

/**
 * Genere et trie tous les coups candidats pour une position donnee.
 * 
 * Optimisations :
 * - Utilise une bounding box autour des pierres existantes (evite de scanner tout le plateau)
 * - Calcule un score de tri pour chaque coup
 * - Trie les coups par score decroissant (meilleurs en premier)
 * - Applique un beam search adaptatif (garde les N meilleurs coups)
 * 
 * Retourne le nombre de coups generes.
 */
int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    // Candidate set incremental O(1)
    int stone_count = g->stone_count;

    if (stone_count == 0 || g->cand_count == 0) {
        moves[0].index = GET_INDEX(9, 9);
        moves[0].score_estim = 1000;
        return 1;
    }

    // Pre-filtre cold : skip candidats avec refcount <= seuil (cases isolees)
    // Desactive en crise locale, pres des feuilles, ou en ouverture
    int opp_local = (player == P1) ? P2 : P1;
    bool local_in_crisis = g->in_crisis
        || g->threat_counts[opp_local][IDX_OPEN_FOUR] > 0
        || g->threat_counts[opp_local][IDX_CLOSED_FOUR] > 0
        || g->captures[opp_local] >= 4;
    bool filter_cold = (depth >= 2) && (stone_count >= 8) && !local_in_crisis;
    int cold_threshold = (stone_count >= 20) ? 2 : 1;
    int km0 = (depth >= 0 && depth < MAX_DEPTH) ? killer_moves[depth][0] : -1;
    int km1 = (depth >= 0 && depth < MAX_DEPTH) ? killer_moves[depth][1] : -1;

    for (int i = 0; i < g->cand_count; i++) {
        int idx = g->cand_list[i];

        // Fast pre-filter O(1) : skip les candidats isolés/périphériques
        if (filter_cold
            && g->cand_refcount[idx] <= cold_threshold
            && idx != tt_best_move
            && idx != km0
            && idx != km1) continue;

        int x = GET_X(idx), y = GET_Y(idx);

        // Double-three pre-filtre (P1 seulement)
        if (player == P1) {
            int pre_atk_dt = get_point_score_fast(g, x, y, player);
            if (pre_atk_dt >= OPEN_THREE && is_double_three(g, idx, player)) continue;
        }

        // Captures pre-calculees (depth >= 2 uniquement)
        int pre_my_caps = (depth >= 2) ? count_potential_captures(g, x, y, player) : 0;
        moves[count].index = idx;
        moves[count].is_capture = (pre_my_caps > 0);
        moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth, pre_my_caps);

        // REJET des coups TROP vulnérables (score négatif)
        if (moves[count].score_estim < -1000000) continue;

        count++;
    }

    // Beam search adaptatif : crise -> beam elargi, sinon reduit pour D10+
    int max_to_keep;
    if (local_in_crisis) {
        // Crise : beam réduit pour D10+ : marge +2-3 vs non-crisis.
        if      (stone_count < 15) max_to_keep = 8;
        else if (stone_count < 25) max_to_keep = 7;
        else if (stone_count < 35) max_to_keep = 6;
        else                       max_to_keep = 6;
    } else {
        // Beam reduit pour atteindre D10+ en mid-game
        int target_beam;
        if      (stone_count < 15) target_beam = 7;
        else if (stone_count < 25) target_beam = 5;
        else if (stone_count < 35) target_beam = 5;
        else                       target_beam = 5;

        if (target_beam < 4) target_beam = 4;
        max_to_keep = target_beam;
    }

    // Extension beam si menaces multiples adverses
    {
        int dmt_opp = (player == P1) ? P2 : P1;
        if (depth >= 4) {
            int opp_open3  = g->threat_counts[dmt_opp][IDX_OPEN_THREE];
            int opp_cls3   = g->threat_counts[dmt_opp][IDX_CLOSED_THREE];
            int opp_cls4   = g->threat_counts[dmt_opp][IDX_CLOSED_FOUR];
            int opp_open2  = g->threat_counts[dmt_opp][IDX_OPEN_TWO];
            bool severe_threat  = (opp_open3 >= 2 || opp_cls4 >= 1
                                   || (opp_open3 >= 1 && opp_cls3 >= 2));
            bool network_threat = (!severe_threat && opp_cls3 >= 2);
            bool building_threat = (!severe_threat && !network_threat && opp_open2 >= 3);
            if (severe_threat) {
                if (max_to_keep + 2 <= 14) max_to_keep += 2;
                else max_to_keep = 14;
            } else if (network_threat) {
                if (max_to_keep + 1 <= 12) max_to_keep += 1;
                else max_to_keep = 12;
            } else if (building_threat) {
                if (max_to_keep + 1 <= 12) max_to_keep += 1;
                else max_to_keep = 12;
            }
        }
    }

    // Depth-dependent narrowing : reduire beam de 1 aux niveaux 1-3 (min 4)
    if (depth >= 1 && depth <= 3 && max_to_keep > 4)
        max_to_keep--;

    // Tri : insertion sort pour petits tableaux, selection partielle pour grands
    int sort_limit = (count < max_to_keep) ? count : max_to_keep;
    if (count <= 32) {
        // Insertion sort
        for (int i = 1; i < count; i++) {
            MoveCandidate key = moves[i];
            int j = i - 1;
            while (j >= 0 && moves[j].score_estim < key.score_estim) {
                moves[j + 1] = moves[j];
                j--;
            }
            moves[j + 1] = key;
        }
    } else {
        // Selection partielle des top-k
        for (int i = 0; i < sort_limit; i++) {
            int best_i = i;
            for (int j = i + 1; j < count; j++) {
                if (moves[j].score_estim > moves[best_i].score_estim)
                    best_i = j;
            }
            if (best_i != i) {
                MoveCandidate tmp = moves[i];
                moves[i] = moves[best_i];
                moves[best_i] = tmp;
            }
        }
    }

    return (count > max_to_keep) ? max_to_keep : count;
}
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
    bool enemy_can_cap = would_capture_us(g, idx, player);
    
    // Evaluation des menaces de lignes creees par ce coup
    // atk_score : valeur offensive (nos menaces)
    // def_score : valeur defensive (menaces adverses bloquees)
    int atk_score = get_point_score(g, x, y, player);
    int def_score = get_point_score(g, x, y, opponent);

    // --- HIÉRARCHIE DE TRI ULTRA-OFFENSIVE ---
    
    // Niveau 0 : Victoire immédiate
    if (atk_score >= WIN_SCORE) return SORT_WIN_IMMEDIATE;
    if (g->captures[player] + (my_caps/2) >= 5) return SORT_WIN_IMMEDIATE;

    // Niveau 1 : Survie CRITIQUE UNIQUEMENT (bloquer victoire adverse)
    if (def_score >= WIN_SCORE) return SORT_BLOCK_WIN + 10000000;
    if (g->captures[opponent] >= 4 && enemy_can_cap) return SORT_BLOCK_WIN + 5000000;

    // Niveau 2 : OFFENSIVE MAXIMALE - Créer menaces AVANT de défendre
    // Un coup qui crée un Four ou plusieurs Threes
    if (atk_score >= OPEN_FOUR) return SORT_THREAT_MAX + 5000000; // Boost massif
    
    // Niveau 2.5 : DOUBLE-FORK OFFENSIF (PRIORITÉ ABSOLUE)
    int fork_value = compute_fork_value(g, idx, player);
    if (fork_value > 0) {
        return fork_value + 2000000; // Boost fourchettes
    }
    
    // OPEN_THREE offensif = PRIORITÉ sur OPEN_FOUR défensif
    if (atk_score >= OPEN_THREE) return SORT_THREAT_MAX;

    // Niveau 2.7 : BLOCAGE DE FOURCHETTE ADVERSE
    // Si l'adversaire jouerait ici pour créer une fourchette (2+ menaces simultanées),
    // on doit occuper cette case avant lui.
    // Priorité juste sous OPEN_THREE offensif : on préfère gagner, mais on bloque
    // une fourchette avant toute autre défense.
    int opp_fork_value = compute_fork_value(g, idx, opponent);
    if (opp_fork_value > 0) {
        return SORT_THREAT_MAX - 500000;
    }

    // Niveau 3 : Défense SECONDARY (après avoir testé offensive)
    if (def_score >= OPEN_FOUR) return SORT_BLOCK_WIN - 1000000; // Réduit priorité
    
    // Niveau 4 : PROTECTION ANTI-CAPTURE
    int vuln_count = count_vulnerable_pairs_after_move(g, idx, player);
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

    // Calcul du score final pour le tri
    // OFFENSIVE x3, DÉFENSE x1.2 (pas de malus sur la défense pure :
    // en crise, bloquer sans contre-attaque est le seul coup utile)
    double att_weight = 3.0;
    double def_weight = 1.2;
    int final_score = (int)(atk_score * att_weight) + (int)(def_score * def_weight) + capture_priority + defense_priority;
    
    // BONUS pour coups offensifs (même faibles)
    if (atk_score >= CLOSED_THREE) {
        final_score = (int)(final_score * 1.5); // +50% pour offensive
    }

    // Bonus de centralité et heuristiques de recherche
    final_score += (20 - (abs(x - 9) + abs(y - 9))) * 100;
    if (depth >= 0 && depth < MAX_DEPTH) {
        if (idx == killer_moves[depth][0]) final_score += SORT_KILLER_1;
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
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty = true;
    int stone_count = 0;

    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty = false;
            stone_count++;
            int cx = GET_X(i), cy = GET_Y(i);
            if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
        }
    }
    
    if (empty) {
        moves[0].index = GET_INDEX(9, 9);
        moves[0].score_estim = 1000;
        return 1;
    }

    // Zone étendue
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;

            // On garde les cases proches des pierres existantes (distance 2)
            bool interesting = false;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] != EMPTY) {
                        interesting = true; break;
                    }
                }
                if (interesting) break;
            }
            if (!interesting) continue;

            // Règle double-three (Renju) : seul P1 (Noir) est soumis à cette restriction.
            // Filtrer ici garantit que negamax ne simule pas de coups illégaux pour P1,
            // évitant des évaluations erronées (l'IA pensait à tort que l'humain peut jouer
            // des double-three → positions adverses surévaluées).
            if (player == P1 && is_double_three(g, idx, player)) continue;

            // Calcul unique des captures : évite 2 appels séparés
            // (1 dans score_move_ordering + 1 pour is_capture).
            int pre_my_caps = count_potential_captures(g, x, y, player);
            moves[count].index = idx;
            moves[count].is_capture = (pre_my_caps > 0);
            moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth, pre_my_caps);

            // REJET des coups TROP vulnérables (score négatif)
            if (moves[count].score_estim < -1000000) {
                continue; // Skip ce coup dangereux
            }
            
            count++;
        }
    }

    // Tri par score décroissant
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // Élagage (Beam Search) : adaptatif selon situation
    // stone_count déjà calculé dans la boucle bbox ci-dessus (Fix E).
    int max_to_keep;
    if (g->in_crisis) {
        // Crise : réduire le beam pour que minimax explore chaque option défensive
        // plus profondément. Avec 60 coups, la profondeur effective est divisée par 12.
        // Avec 12, on concentre la recherche sur les seuls coups qui comptent.
        max_to_keep = 12;
    } else if (stone_count < 15) {
        max_to_keep = 35; // Début de partie : plus de coups
    } else if (stone_count < 40) {
        // Mid-game : 30 coups pour ne pas couper les moves de construction adverse
        // (score CLOSED_THREE ~50k) qui tombaient à l'index 20-25 avec l'ancien beam.
        max_to_keep = 30;
    } else {
        max_to_keep = 30; // End-game : plus d'options
    }
    
    return (count > max_to_keep) ? max_to_keep : count;
}
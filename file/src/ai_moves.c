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
    // Guard depth >= 3 : would_capture_us simule un coup adverse (board[idx]=opp +
    // count_potential_captures 8 dirs + reset). Coût ~20 ops par candidat.
    // Aux depth <= 2 (90% des noeuds), enemy_can_cap n'est utilisé que pour
    // SORT_BLOCK_WIN + 5000000 (captures[opponent]>=4) ou defense_priority.
    // Ces deux cas sont très rares aux nœuds profonds et couverts par l'éval incrémentale.
    bool enemy_can_cap = (depth >= 3) ? would_capture_us(g, idx, player) : false;
    
    // Evaluation des menaces de lignes creees par ce coup
    // atk_score : valeur offensive (nos menaces)
    // def_score : valeur defensive (menaces adverses bloquees)
    //
    // Perf : aux noeuds internes (depth <= 4), on utilise get_point_score_fast
    // (scan directionnel simple, pas de fork_bonus ni latent threats). ~3-4× plus rapide.
    // La hiérarchie WIN/OPEN_FOUR/OPEN_THREE/CLOSED_THREE reste correcte (seuils max
    // par direction). Depth 3-4 contient la majorité des noeuds; depth 5+ (proches racine)
    // utilisent le scoring complet pour qualité d'ordonnancement à la racine.
    int atk_score, def_score;
    if (depth <= 4) {
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
    // Un coup qui crée un Four ou plusieurs Threes
    if (atk_score >= OPEN_FOUR) return SORT_THREAT_MAX + 5000000; // Boost massif
    
    // Niveau 2.5a : BLOCAGE FOURCHETTE ADVERSE (avant tout OPEN_THREE)
    // Guard ADAPTATIF selon la profondeur courante :
    // - depth >= 5 (proche racine) : trigger élargi si adversaire a ≥2 OPEN_TWO
    //   (global_fork_threat). Peu de noeuds à ce niveau, coût acceptable.
    // - depth 3-4 : seulement si def_score local >= CLOSED_THREE.
    // - depth <= 2 : JAMAIS. Ces noeuds sont les plus nombreux (~90% de l'arbre)
    //   et compute_fork_value (simulate + 4×evaluate_line + is_double_three)
    //   multipliait le coût de gen par ~3×.
    int opp_fork_value = 0;
    bool global_fork_threat = (depth >= 5) && (g->threat_counts[opponent][IDX_OPEN_TWO] >= 2);
    if (depth >= 3 && (def_score >= CLOSED_THREE || global_fork_threat)) {
        opp_fork_value = compute_fork_value(g, idx, opponent);
    }
    if (opp_fork_value > 0) {
        return SORT_THREAT_MAX + 1000000;
    }

    // Niveau 2.5b : DOUBLE-FORK OFFENSIF
    // Guard depth >= 3 : même raison que ci-dessus.
    int fork_value = 0;
    if (depth >= 3 && atk_score >= CLOSED_THREE) {
        fork_value = compute_fork_value(g, idx, player);
    }
    if (fork_value > 0) {
        return SORT_THREAT_MAX + 3000000;
    }
    
    // OPEN_THREE offensif
    if (atk_score >= OPEN_THREE) return SORT_THREAT_MAX;

    // Niveau 3 : Défense SECONDARY (après avoir testé offensive)
    if (def_score >= OPEN_FOUR) return SORT_BLOCK_WIN - 1000000; // Réduit priorité
    
    // Niveau 4 : PROTECTION ANTI-CAPTURE
    // Guard depth >= 3 : count_vulnerable_pairs_after_move simule un coup
    // (board[idx]=player + scan 4 dirs + reset). Coût identique à compute_fork_value.
    // Aux depth <= 2 (la majorité des noeuds), le killer/TT ordonne déjà les coups
    // défensifs importants. La vulnérabilité capture est couverte par is_capture.
    int vuln_count = (depth >= 3) ? count_vulnerable_pairs_after_move(g, idx, player) : 0;
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

    // BONUS DE CONNEXION : O(1) via cand_refcount qui compte exactement le nombre
    // de pierres (toute couleur) dans dist≤2. Remplace la boucle 5×5 (25 iters)
    // qui recalculait la même information depuis le plateau.
    // cand_refcount=2 → ≈ 1 voisin proche  → bonus faible
    // cand_refcount=6 → ≈ quelques voisins → bonus moyen
    // cand_refcount>=10 → case très connectée → bonus fort
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
    // O(1) grâce au candidate set incrémental.
    // stone_count et cand_list sont maintenus en O(25) par apply_move/undo_move.
    int stone_count = g->stone_count;

    if (stone_count == 0 || g->cand_count == 0) {
        moves[0].index = GET_INDEX(9, 9);
        moves[0].score_estim = 1000;
        return 1;
    }

    // Itération directe sur les candidates — pas de scan bbox, pas de vérification voisins.
    // Chaque case dans cand_list est garantie : EMPTY && au moins 1 pierre dans dist≤2.
    //
    // PRÉ-FILTRE "cold" : skip les candidats avec refcount==1 (exactement 1 pierre
    // dans dist≤2 — case isolée sans valeur tactique réelle).
    // Conditions de sécurité :
    //   depth >= 2  : ne pas filtrer près des feuilles (quiescence depth=-1, leaves depth=0-1)
    //   stone_count >= 8 : pas en ouverture (peu de candidats, filtrer = risque qualité)
    //   !in_crisis  : en crise, ne rien manquer
    // Exceptions : tt_best_move + killer moves = toujours inclus même si cold
    //              (ces coups viennent d'itérations précédentes, peuvent être déplacés)
    // Gain attendu : cand_count ~60 → ~25 hot scorés → ×2.4 cheaper.
    bool filter_cold = (depth >= 2) && (stone_count >= 8) && !g->in_crisis;
    int km0 = (depth >= 0 && depth < MAX_DEPTH) ? killer_moves[depth][0] : -1;
    int km1 = (depth >= 0 && depth < MAX_DEPTH) ? killer_moves[depth][1] : -1;

    for (int i = 0; i < g->cand_count; i++) {
        int idx = g->cand_list[i];

        // Fast pre-filter O(1) : skip les candidats isolés
        if (filter_cold
            && g->cand_refcount[idx] <= 1
            && idx != tt_best_move
            && idx != km0
            && idx != km1) continue;

        int x = GET_X(idx), y = GET_Y(idx);

        // Règle double-three (Renju) : seul P1 (Noir) est soumis à cette restriction.
        if (player == P1 && is_double_three(g, idx, player)) continue;

        // Calcul unique des captures : évite 2 appels séparés.
        int pre_my_caps = count_potential_captures(g, x, y, player);
        moves[count].index = idx;
        moves[count].is_capture = (pre_my_caps > 0);
        moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth, pre_my_caps);

        // REJET des coups TROP vulnérables (score négatif)
        if (moves[count].score_estim < -1000000) continue;

        count++;
    }

    // Tri par score décroissant
    qsort(moves, count, sizeof(MoveCandidate), compare_moves);

    // Élagage (Beam Search) : adaptatif selon situation
    // stone_count déjà calculé dans la boucle bbox ci-dessus (Fix E).
    int max_to_keep;
    if (g->in_crisis) {
        // Crise : beam restreint pour exploration défensive profonde.
        max_to_keep = 12;
    } else {
        // Base par phase de jeu
        // Réduire le beam dans les premières phases permet d'atteindre depth 6+
        // en early/mid game — c'est là que P1 construit des fourches silencieuses
        // sur 3 coups. Beam plein (35) bloque à depth 4 (35^3 = 42875 nœuds vs
        // 18^3 = 5832 avec base=18), insuffisant pour voir un plan à 3 demi-coups.
        // Combiné au +50ms de budget, on gagne ~1 ply sur les positions défensives
        // critiques sans dépasser 0.5s.
        int base;
        if (stone_count < 15)      base = 20;   // ouverture : beam plein pour qualité
        else if (stone_count < 40) base = 20;   // milieu
        else                       base = 18;   // fin de partie

        // Réduction beam sur les nœuds INTERNES (depth > 0).
        // La racine (depth == 0, juste avant quiescence) garde déjà -20%.
        // Sur les nœuds depth 1+, réduire de 35% libère ~3× plus de nœuds
        // qu'à la racine seule, ce qui permet +2 ply de profondeur.
        // Beam 20 → 13 sur nœuds internes : 20^4=160000 → 13^4=28561 (-82%).
        // Risque : retirer des coups utiles en early game. Guard : ne s'applique
        // qu'à depth >= 2 (les 2 premiers niveaux gardent beam plein pour qualité).
        // depth == 0 : feuilles quiescence → -20% (inchangé)
        // depth == 1 : 1 ply avant feuille → beam plein (blocages immédiats)
        // depth == 2 : 2 plies avant feuille → beam plein (réponses adverses silencieuses
        //              qui ne sont pas encore des menaces classifiées : fourches en préparation,
        //              captures futures). Réduire ici éliminait ces coups → scores 0, oscillations.
        // depth >= 3 : nœuds internes profonds → beam réduit à 13.
        //              À ce niveau, le TT + killer moves couvrent les coups importants.
        //              13^3 = 2197 nœuds vs 20^3 = 8000 → ×3.6 moins cher → +2 ply.
        if (depth == 0)       base = (base * 8) / 10;
        else if (depth == 2)  base = (base * 12) / 20;  // beam 12 at depth 2 (was 14)
        else if (depth >= 3)  base = (base * 11) / 20;  // beam 11 at depth≥3 (was 13)

        if (base < 8) base = 8; // toujours au moins 8 coups explorés
        max_to_keep = base;
    }
    
    return (count > max_to_keep) ? max_to_keep : count;
}
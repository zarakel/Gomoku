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
    // Guard depth >= 5 : would_capture_us simule un coup adverse (board[idx]=opp +
    // count_potential_captures 8 dirs + reset). Coût ~150 ns/candidat.
    // Mesure sur les logs : depth 3-4 = majorité des nœuds (>60% de l'arbre D8).
    // À ces profondeurs, enemy_can_cap influe sur defense_priority (SORT_BLOCK_WIN/4)
    // et le bloc SORT_BLOCK_WIN+5000000 (captures[opponent]>=4). Ces cas sont rares
    // et couverts correctement par le score incrémental + TT warm des itérations
    // précédentes. Économie : ~3 μs/nœud → ~50ms sur D8 à T1.
    bool enemy_can_cap = (depth >= 5) ? would_capture_us(g, idx, player) : false;
    
    // Evaluation des menaces de lignes creees par ce coup
    // atk_score : valeur offensive (nos menaces)
    // def_score : valeur defensive (menaces adverses bloquees)
    //
    // Perf : aux noeuds internes (depth <= 6), on utilise get_point_score_fast
    // (scan directionnel simple, pas de fork_bonus ni latent threats). ~3-4× plus rapide.
    // La hiérarchie WIN/OPEN_FOUR/OPEN_THREE/CLOSED_THREE reste correcte (seuils max
    // par direction). Dans un arbre D8, depth 5-6 = centaines de noeuds.
    // On ne garde get_point_score complet qu'aux depth 7-8 (racine + 1 niveau sous racine
    // = 1+11=12 noeuds max) où la qualité d'ordonnancement impacte directement le coup joué.
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
    // Exception CRITIQUE capture-3 : si l'adversaire a 3 paires capturées,
    // il lui suffit de 2 paires de plus pour gagner. Un coup offensif OPEN_FOUR
    // qui expose simultanément des pierres peut offrir la 4e+5e capture gagnante.
    // On sacrifie le boost OPEN_FOUR pour vérifier la vulnérabilité, même à depth<5.
    // Guard : seulement si g->captures[opponent] >= 3 (cas rare, coût acceptable).
    if (atk_score >= OPEN_FOUR) {
        if (g->captures[opponent] >= 3) {
            int vuln = count_vulnerable_pairs_after_move(g, idx, player);
            if (vuln > 0) {
                // Pénalité proportionnelle : chaque paire exposée peut mener à la défaite.
                // Score négatif pour sortir du beam — l'IA doit trouver un coup
                // offensif qui ne s'offre pas en même temps.
                return -(OPEN_FOUR / 2) * vuln;
            }
        }
        return SORT_THREAT_MAX + 5000000; // Boost massif
    }
    
    // Niveau 2.5a : BLOCAGE FOURCHETTE ADVERSE (avant tout OPEN_THREE)
    // Guard STRICT depth >= 6 : compute_fork_value = simulate + 4×evaluate_line +
    // is_double_three(4×evaluate_line + count_potential_captures) = ~128 ops/candidat.
    // Expérience : depth>=4 avec trigger OPEN_THREE faisait exploser le coût sur les
    // positions tactiques (D4 à 0.40s observé), supprimant D8 sur T2-T3.
    // À depth>=6 (1-2 plies sous racine), ~12 noeuds max → coût négligeable.
    // La TT warm des itérations précédentes propage l'info de fork aux noeuds profonds.
    int opp_fork_value = 0;
    bool global_fork_threat = (depth >= 6) && (g->threat_counts[opponent][IDX_OPEN_TWO] >= 2);
    if (depth >= 6 && (def_score >= CLOSED_THREE || global_fork_threat)) {
        opp_fork_value = compute_fork_value(g, idx, opponent);
    }
    if (opp_fork_value > 0) {
        return SORT_THREAT_MAX + 1000000;
    }

    // Niveau 2.5b : DOUBLE-FORK OFFENSIF — guard depth >= 6, même raison.
    int fork_value = 0;
    if (depth >= 6 && atk_score >= CLOSED_THREE) {
        fork_value = compute_fork_value(g, idx, player);
    }
    if (fork_value > 0) {
        return SORT_THREAT_MAX + 3000000;
    }
    
    // OPEN_THREE offensif
    if (atk_score >= OPEN_THREE) return SORT_THREAT_MAX;

    // Niveau 2.7 : DUAL-PURPOSE — bloque une menace adverse ≥ OPEN_THREE ET crée
    // notre propre menace ≥ CLOSED_THREE simultanément.
    // Ces coups sont la clé en position déficitaire : ils réduisent la pression
    // adverse tout en construisant notre propre jeu.
    // Tier : entre SORT_THREAT_MAX et SORT_CAPTURE, valeur 12M.
    // Guard : def_score >= OPEN_THREE (valide la partie défense), atk_score >=
    // CLOSED_THREE (valide la partie offensive — même créer un CLOSED_THREE est utile).
    if (def_score >= OPEN_THREE && atk_score >= CLOSED_THREE) {
        // Bonus proportionnel à la qualité offensive : CLOSED_THREE=base, OPEN_THREE=+2M
        int dual_bonus = 12000000 + (atk_score / 10);
        return dual_bonus;
    }

    // Niveau 2.8 : BLOCAGE CLOSED_FOUR adverse — menace à 1 coup de la victoire.
    // CLOSED_FOUR = 4 pierres + 1 extrémité ouverte. L'adversaire joue l'extrémité → victoire.
    // Sans ce niveau, def_score=5M tombe dans le score pondéré final (5M×1.2=6M), insuffisant
    // pour battre des coups offensifs à 8-12M → closed four JAMAIS bloqué dans le beam.
    // Priorité 25M : plus urgent qu'un OPEN_THREE offensif (30M? non, en-dessous) mais plus
    // urgent que dual-purpose (12M). On reste sous OPEN_THREE offensif (30M) car créer une
    // menace offensive en bloquant un closed four est idéal, mais bloquer seul vaut déjà 25M.
    // Guard : CLOSED_FOUR <= def_score < OPEN_FOUR pour ne pas interférer avec le niveau 3.
    if (def_score >= CLOSED_FOUR && def_score < OPEN_FOUR) return 25000000 + (atk_score / 100);

    // Niveau 3 : Défense SECONDARY (après avoir testé offensive)
    if (def_score >= OPEN_FOUR) return SORT_BLOCK_WIN - 1000000; // Réduit priorité
    
    // Niveau 4 : PROTECTION ANTI-CAPTURE
    // Guard depth >= 5 unconditional : count_vulnerable_pairs_after_move simule un coup.
    // depth 4 avec trigger conditonnel causait des D4@0.40s dans les positions tactiques.
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
        // PRÉ-FILTRE : is_double_three exige 2+ open_threes simultanés.
        // get_point_score_fast donne le max sur toutes les directions.
        // Si max < OPEN_THREE → aucune direction ne crée OPEN_THREE → impossible
        // d'en créer 2 → skip le scan coûteux (4×evaluate_line + potentiel
        // count_potential_captures = ~500 ns/candidat).
        // Économie : ~80-95% des candidats en position calme, ~50% en tactique.
        if (player == P1) {
            int pre_atk_dt = get_point_score_fast(g, x, y, player);
            if (pre_atk_dt >= OPEN_THREE && is_double_three(g, idx, player)) continue;
        }

        // Calcul unique des captures : évite 2 appels séparés.
        // Guard depth >= 2 : aux feuilles (depth 0-1), is_capture sert uniquement
        // au filtre QS qui génère ses propres candidats avec ses propres is_capture.
        // Skip le scan 8-dirs ici évite ~30% du coût de generate_moves aux feuilles.
        int pre_my_caps = (depth >= 2) ? count_potential_captures(g, x, y, player) : 0;
        moves[count].index = idx;
        moves[count].is_capture = (pre_my_caps > 0);
        moves[count].score_estim = score_move_ordering(g, idx, player, tt_best_move, depth, pre_my_caps);

        // REJET des coups TROP vulnérables (score négatif)
        if (moves[count].score_estim < -1000000) continue;

        count++;
    }

    // Élagage (Beam Search) : adaptatif selon situation.
    // Calculer max_to_keep AVANT le tri pour permettre un tri partiel (plus rapide).
    int max_to_keep;
    if (g->in_crisis) {
        // Crise : beam adaptatif aussi selon stone_count.
        // Un beam=12 fixe sur 25+ pierres explose l'arbre (D6 seulement observé).
        // La défense en crise n'a pas besoin de plus de candidats que le mid-game
        // normal + un delta de 4 pour couvrir les réponses défensives supplémentaires.
        // stone_count < 15 : 12 (ouverture, peu de candidats, margin ok)
        // stone_count 15-24 : 10 (early mid, crise rare, D8 accessible)
        // stone_count 25-34 : 8  (mid-game dense, D8 sous 0.25s)
        // stone_count >= 35 : 7  (fin de partie, tout est tactique)
        if      (stone_count < 15) max_to_keep = 12;
        else if (stone_count < 25) max_to_keep = 10;
        else if (stone_count < 35) max_to_keep = 8;
        else                       max_to_keep = 7;
    } else {
        // Beam adaptatif selon avancement : plus de pierres = arbre plus dense,
        // il faut réduire le beam pour conserver de la profondeur.
        //   < 15 pierres  : beam=8 → ouverture + early game, qualité maximale
        //   15-24 pierres : beam=7 → milieu de jeu, réduit ~41% le coût D8
        //   25-34 pierres : beam=6 → mid-game dense, D10 reste accessible
        //   >= 35 pierres : beam=5 → fin de partie, D10+
        int target_beam;
        if      (stone_count < 15) target_beam = 8;
        else if (stone_count < 25) target_beam = 7;
        else if (stone_count < 35) target_beam = 6;
        else                       target_beam = 5;

        int base;
        if (depth == 0) base = target_beam + 2;  // root légèrement plus large pour qualité
        else            base = target_beam;

        if (base < 4) base = 4;
        max_to_keep = base;
    }

    // DUAL-THREAT EXTENSION : si l'adversaire construit plusieurs menaces simultanées
    // (2+ Open Threes ou 2+ Closed Fours), le beam standard risque de rater la
    // contre-menace nécessaire. Élargir de +2, plafonner à 12 pour ne pas tuer depth 10.
    // Conditionné à depth >= 4 pour ne pas impacter les feuilles (QS gère depth 0-3).
    // Étendu : 1+ Closed Four aussi (l'adversaire peut promouvoir en 1 coup → fourchette).
    {
        int dmt_opp = (player == P1) ? P2 : P1;
        if (depth >= 4
            && (g->threat_counts[dmt_opp][IDX_OPEN_THREE] >= 2
                || g->threat_counts[dmt_opp][IDX_CLOSED_FOUR] >= 1)) {
            if (max_to_keep + 2 <= 12) max_to_keep += 2;
            else max_to_keep = 12;
        }
    }

    // Tri partiel : ne trier que les max_to_keep premiers éléments.
    // Full qsort est O(n log n) avec overhead fonction-pointeur. Pour beam<=14,
    // une sélection partielle est O(n * k) avec n~20-30, k~8-14 :
    //   n=25, k=8 → 200 comparaisons sans overhead vs qsort ~115 + overhead fp.
    // Net gain sur les nœuds profonds (depth>=3) où k=8.
    // Pour les rares cas count > 40 (ouverture, pas de filter_cold), qsort reste
    // préférable car O(n log n) bat O(n*k) quand n >> k.
    int sort_limit = (count < max_to_keep) ? count : max_to_keep;
    if (count <= 32) {
        // Insertion sort : optimal pour tableaux petits quasi-triés
        // (TT move scoring est déjà SORT_HASH → en tête, killers proches du haut).
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
        // Selection partielle : pour les tableaux plus grands (ouverture),
        // trier uniquement les sort_limit premiers éléments nécessaires.
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
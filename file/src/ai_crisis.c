#include "../include/gomoku.h"

/**
 * Moteur de detection de situations critiques.
 * 
 * Analyse le plateau pour detecter les menaces adverses imparables.
 * Calcule un niveau de crise (0-3) selon la gravite de la situation.
 * 
 * Niveaux de crise :
 * 0 = Pas de crise (jeu normal)
 * 1 = Menace serieuse (Open 3 ou mieux)
 * 2 = Menace critique (Open 4 ou plusieurs Open 3)
 * 3 = Mort imminente (Plusieurs Open 4 ou VCF detecte)
 * 
 * Note : Le systeme de crise est INFORMATIF uniquement.
 * Il ne force plus les coups (cette logique a ete supprimee dans makeIaMove).
 */

/**
 * Verifie si jouer a l'index 'idx' est une victoire immediate pour 'opponent'.
 * Teste la victoire par alignement (5) et par capture (5 paires).
 */
bool is_winning_threat(game *g, int idx, int opponent) {
    if (g->board[idx] != EMPTY) return false;
    
    // 1. Victoire par alignement (5)
    g->board[idx] = opponent;
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
    g->board[idx] = EMPTY;
    
    if (score >= WIN_SCORE) return true;
    
    // 2. Victoire par capture (si règle active)
    // Seuil abaissé à >= 2 : si P1 a 2 captures et peut capturer 3 paires en 1 coup
    // (6 pierres, caps/2 = 3), il atteint 5 → victoire.  Avec >= 3 ce cas était
    // invisible → is_winning_threat retournait false → défense jamais déclenchée.
    if (g->captures[opponent] >= 2) {
        int caps = count_potential_captures(g, GET_X(idx), GET_Y(idx), opponent);
        if (g->captures[opponent] + (caps / 2) >= 5) return true;
    }
    
    return false;
}

/**
 * Compte les menaces immediates serieuses de l'adversaire.
 * 
 * Scanne la bounding box autour des pierres existantes.
 * Detecte les coups adverses qui creent des menaces CLOSED_FOUR, OPEN_FOUR ou superieures.
 * 
 * Remplit threat_moves avec les indices des coups menacants (max 10).
 * Retourne le nombre de menaces detectees.
 */
static int count_immediate_threats(game *g, int opponent, int *threat_moves) {
    int count = 0;

    if (g->cand_count == 0) return 0;

    // itération sur cand_list au lieu d'un scan bbox O(200-400).
    // cand_list garantit exactement les cases EMPTY avec ≥1 voisin dist≤2,
    // soit ~20-60 cases en mid-game — ×5-10 plus rapide que la bbox.
    for (int ci = 0; ci < g->cand_count; ci++) {
        int idx = g->cand_list[ci];
        int x = GET_X(idx), y = GET_Y(idx);

        // Vérifier si ce coup crée une menace
        g->board[idx] = opponent;
        int score = get_point_score(g, x, y, opponent);
        g->board[idx] = EMPTY;

        // Menace sérieuse : Open 4 ou mieux
        if (score >= OPEN_FOUR) {
            if (count < 10)
                threat_moves[count++] = idx;
            continue; // Déjà compté, pas besoin de checker captures
        }

        // A6-FIX: Menace précurseur : Closed Four (à 1 coup de devenir Open Four).
        // Un Closed Four non bloqué = promotion garantie au tour suivant.
        // On les met dans crisis_moves AUSSI pour que find_best_defense les évalue.
        // Niveau <= CLOSED_FOUR non-dupliqué avec l'OPEN_FOUR ci-dessus.
        if (score >= CLOSED_FOUR && count < 10) {
            threat_moves[count++] = idx;
            continue;
        }

        // Menace de capture : si adversaire a >= 3 paires et ce coup
        // crée une capture (l'amenant à 4+), c'est une menace sérieuse
        // qu'il faut inclure dans crisis_moves pour blocage préventif.
        if (g->captures[opponent] >= 3) {
            int caps = count_potential_captures(g, x, y, opponent);
            if (caps >= 2 && g->captures[opponent] + (caps / 2) >= 4) {
                if (count < 10)
                    threat_moves[count++] = idx;
            }
        }
    }

    return count;
}

/**
 * Analyse la situation actuelle et met a jour l'etat de crise.
 * 
 * Detecte dans l'ordre :
 * 1. Victoires immediates adverses (niveau 3)
 * 2. Menaces Open Four (niveau 2-3)
 * 3. Menaces Open Three multiples (niveau 1-2)
 * 4. Danger de capture (5eme paire adverse)
 * 
 * Met a jour les champs g->in_crisis, g->crisis_level, g->crisis_moves.
 * Cette fonction est INFORMATIVE uniquement (les coups ne sont plus forces automatiquement).
 */
void update_crisis_state(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    g->in_crisis = false;
    g->crisis_immediate_win = false;
    g->crisis_level = 0;
    g->crisis_move_count = 0;
    
    // 1. VICTOIRE IMMÉDIATE ADVERSE (Urgence absolue)
    // threat_counts[IDX_WIN] est maintenu en O(1) par apply_move/undo_move.
    // L'ancien scan porte sur 361 cases × get_point_score (64+ ops) = 23K ops/tour.
    // On localise les coups gagnants UNIQUEMENT si threat_counts > 0 (rare).
    int winning_moves = 0;

    if (g->threat_counts[opponent][IDX_WIN] > 0 || g->captures[opponent] >= 4) {
        // Localiser les cases gagnantes : on itère sur cand_list (cases vides proches)
        for (int ci = 0; ci < g->cand_count; ci++) {
            int i = g->cand_list[ci];
            if (is_winning_threat(g, i, opponent)) {
                g->crisis_moves[winning_moves++] = i;
                if (winning_moves >= 10) break;
            }
        }
    }
    
    if (winning_moves > 0) {
        g->in_crisis = true;
        g->crisis_immediate_win = true; // L'adversaire gagne en 1 coup → VCF offensif interdit
        g->crisis_move_count = winning_moves;
        g->crisis_level = (winning_moves == 1) ? 2 : 3; // 3 = Mort quasi certaine (double menace)

        // Si la menace est par captures (captures[opponent] >= 4), on peut aussi
        // bloquer via CONTRE-CAPTURE : réduire captures[opponent] 4→3 annule la menace.
        // Ajouter les cases de contre-capture dans crisis_moves pour que
        // find_best_defense_with_threat_space les évalue comme candidats défensifs.
        if (g->captures[opponent] >= 4 && winning_moves < 10) {
            for (int ci = 0; ci < g->cand_count && winning_moves < 10; ci++) {
                int k = g->cand_list[ci];
                int caps = count_potential_captures(g, GET_X(k), GET_Y(k), ia_player);
                if (caps >= 2) {
                    // Vérifie que ce coup n'est pas déjà dans crisis_moves
                    bool already = false;
                    for (int m = 0; m < winning_moves; m++) {
                        if (g->crisis_moves[m] == k) { already = true; break; }
                    }
                    if (!already)
                        g->crisis_moves[winning_moves++] = k;
                }
            }
            // S'il y a des contre-captures disponibles, monter crisis_level à 3
            // pour forcer find_best_defense_with_threat_space
            if (g->crisis_move_count < winning_moves) {
                g->crisis_move_count = winning_moves;
                g->crisis_level = 3;
            }
        }

        #ifdef DEBUG
        bool by_capture = (g->captures[opponent] >= 4);
        printf("[CRISIS] lvl=%d wins=%d [%s]\n",
               g->crisis_level, g->crisis_move_count,
               by_capture ? "CAPTURE" : "ALIGN");
        #endif
        return;
    }
    
    // 2. Check menaces fortes (Open 4 et Closed 4)
    // A6-FIX: count_immediate_threats collecte aussi les CLOSED_FOUR dans crisis_moves.
    // On compte séparément : open_four purs (score >= OPEN_FOUR) pour le niveau de crise,
    // et closed_four (CLOSED_FOUR <= score < OPEN_FOUR) pour l'avertissement précoce.
    int all_threat_count = count_immediate_threats(g, opponent, g->crisis_moves);

    // Distinguer open_fours (OPEN_FOUR+) des closed_fours en re-simulant les scores.
    // On ne re-simule que pour les menaces collectées (max 10) — coût négligeable.
    int open_four_count = 0;
    int closed_four_count = 0;
    for (int ti = 0; ti < all_threat_count; ti++) {
        int idx = g->crisis_moves[ti];
        g->board[idx] = opponent;
        int sc = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
        g->board[idx] = EMPTY;
        if (sc >= OPEN_FOUR)  open_four_count++;
        else                  closed_four_count++;
    }
    g->crisis_move_count = all_threat_count;

    if (open_four_count > 0) {
        g->in_crisis = true;
        g->crisis_move_count = all_threat_count;  // inclut aussi les closed_four pour que find_best_defense les évalue
        g->crisis_level = (open_four_count >= 2) ? 3 : 2;

        // P1-FIX: Si adversaire est aussi à captures >= 4, une contre-capture peut
        // neutraliser sa victoire par capture ET bloquer l'open four.
        // On fusionne les contre-captures dans crisis_moves pour que
        // find_best_defense_with_threat_space les évalue côté capture.
        if (g->captures[opponent] >= 4) {
            int wm = g->crisis_move_count;
            for (int ci = 0; ci < g->cand_count && wm < 10; ci++) {
                int k = g->cand_list[ci];
                if (g->board[k] != EMPTY) continue;
                int caps = count_potential_captures(g, GET_X(k), GET_Y(k), ia_player);
                if (caps >= 2) {
                    bool already = false;
                    for (int m = 0; m < wm; m++) {
                        if (g->crisis_moves[m] == k) { already = true; break; }
                    }
                    if (!already)
                        g->crisis_moves[wm++] = k;
                }
            }
            if (wm > g->crisis_move_count) {
                g->crisis_move_count = wm;
                g->crisis_level = 3;  // force find_best_defense_with_threat_space
                #ifdef DEBUG
                printf("[CRISIS] P1-FIX: %d counter-captures merged\n", wm - open_four_count);
                #endif
            }
        }

        #ifdef DEBUG
        printf("[CRISIS] lvl=%d of=%d\n", g->crisis_level, open_four_count);
        #endif
        return;
    }
    if (closed_four_count > 0) {
        // A6-FIX: Closed Four sans Open Four = précurseur urgent (niveau 1).
        // L'adversaire peut promouvoir en Open Four ou Gapped Four en 1 seul coup.
        // On déclenche la crise niveau 1 pour que DEFENSE-RESCUE soit disponible
        // si minimax retourne un score perdant (-WIN_SCORE range).
        g->in_crisis = true;
        g->crisis_move_count = closed_four_count;
        // Copier seulement les closed_four en tête de crisis_moves
        // (les open_four étaient à zéro ici, donc les closed_four sont en tête)
        g->crisis_level = 1;
        #ifdef DEBUG
        printf("[CRISIS] lvl=1 cf=%d\n", closed_four_count);
        #endif
        return;
    }

    // 3. Check menaces moyennes (Open 3) -> AJOUT CRITIQUE ICI
    // Si aucune menace mortelle, on cherche les Open 3 pour les ajouter à la liste de crise
    int open_three_count = g->threat_counts[opponent][IDX_OPEN_THREE];
    
    if (open_three_count > 0) {
        // Scanner via cand_list au lieu de 19×19 hardcodé.
        // cand_list = cases vides avec au moins 1 voisin dist≤ 2 = exactement
        // les cases où une menace peut être complétée.
        int count_found = 0;

        for (int ci = 0; ci < g->cand_count; ci++) {
            int idx = g->cand_list[ci];

            g->board[idx] = opponent;
            int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
            g->board[idx] = EMPTY;

            if (score >= OPEN_FOUR) {
                if (count_found < 10)
                    g->crisis_moves[count_found++] = idx;
            }
        }
        
        if (count_found > 0) {
            g->crisis_move_count = count_found;
            g->in_crisis = true;
            g->crisis_level = 1; // Niveau prudence
            if (count_found >= 2) g->crisis_level = 2; // Double menace = Danger
            
            #ifdef DEBUG
            printf("[CRISIS] lvl=%d ot=%d\n", g->crisis_level, count_found);
            #endif
            return;
        }
    }
    
    // 4. Check captures préventif
    // Déclenche dès >= 3 paires capturées : si l'adversaire peut atteindre 4+
    // en 1 coup sur des paires vulnérables, c'est une menace critique.
    // Avant : seuil à >= 4 → la crise ne se déclenchait qu'au bord du gouffre.
    if (g->captures[opponent] >= 3) {
        int vulnerable = count_vulnerable_pairs(g, ia_player);
        if (vulnerable > 0) {
            g->in_crisis = true;
            g->crisis_level = (g->captures[opponent] >= 4) ? 3 : 2;
            #ifdef DEBUG
            printf("[CRISIS] lvl=%d opp_cap=%d vuln=%d\n",
                   g->crisis_level, g->captures[opponent], vulnerable);
            #endif
            return;
        }
    }
}

/*
 * ============================================================================
 * THREAT SPACE SEARCH
 * ============================================================================
 * Au lieu de tester les coups un par un, on cherche les INTERSECTIONS
 * de menaces (coups qui bloquent plusieurs lignes simultanément).
 */

typedef struct {
    int idx;
    int blocked_threats;
    int combined_score;
} DefenseCandidate;

static int compare_defense(const void *a, const void *b) {
    return ((DefenseCandidate *)b)->combined_score - ((DefenseCandidate *)a)->combined_score;
}

/*
 * Trouve les cases qui bloquent physiquement une menace
 */
static void add_blocking_moves(game *g, int threat_idx, int ia_player, int *counts) {
    int opponent = (ia_player == P1) ? P2 : P1;
    int tx = GET_X(threat_idx);
    int ty = GET_Y(threat_idx);

    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    // La case de menace elle-même est toujours le meilleur blocage
    if (g->board[threat_idx] == EMPTY)
        counts[threat_idx] += 10;

    for (int d = 0; d < 4; d++) {
        for (int k = -4; k <= 4; k++) {
            if (k == 0) continue;
            int nx = tx + dx[d] * k;
            int ny = ty + dy[d] * k;
            if (!IS_VALID(nx, ny)) continue;
            int idx = GET_INDEX(nx, ny);
            if (g->board[idx] != EMPTY) continue;

            // Simulation 1 : plateau tel quel, adversaire joue en threat_idx
            // → score de référence (menace active)
            g->board[threat_idx] = opponent;
            int score_before = get_point_score(g, tx, ty, opponent);
            g->board[threat_idx] = EMPTY; // reset propre

            // Simulation 2 : on bloque en idx, adversaire joue en threat_idx
            // → score après notre blocage
            g->board[idx] = ia_player;
            g->board[threat_idx] = opponent;
            int score_after = get_point_score(g, tx, ty, opponent);
            g->board[threat_idx] = EMPTY; // reset propre
            g->board[idx] = EMPTY;        // reset propre

            // Bon blocage si la menace tombe sous OPEN_FOUR
            if (score_before >= OPEN_FOUR && score_after < OPEN_FOUR) {
                counts[idx] += 2;
            }
        }
    }
}

/*
 * LOGIQUE PRINCIPALE DE DÉFENSE
 */
int find_best_defense_with_threat_space(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    if (g->crisis_move_count == 0) return -1;

    int blocking_weights[MAX_BOARD];
    memset(blocking_weights, 0, sizeof(blocking_weights));
    
    // 1. Carte de chaleur des blocages
    for (int i = 0; i < g->crisis_move_count; i++) {
        add_blocking_moves(g, g->crisis_moves[i], ia_player, blocking_weights);
    }

    // 1b. CONTRE-CAPTURES : quand l'adversaire est à 4 captures, une contre-capture
    // (prendre une de ses paires) réduit captures[opponent] de 4→3 et annule la menace.
    // Ces cases ne sont pas détectées par add_blocking_moves (qui teste le score OPEN_FOUR).
    // On leur attribue un poids élevé (8) pour les placer en tête des candidats.
    if (g->captures[opponent] >= 4) {
        for (int ci = 0; ci < g->cand_count; ci++) {
            int k = g->cand_list[ci];
            if (g->board[k] != EMPTY) continue;
            int caps = count_potential_captures(g, GET_X(k), GET_Y(k), ia_player);
            if (caps >= 2) {
                blocking_weights[k] += 8; // Poids fort : contre-capture annule la menace
            }
        }
    }

    // 2. Génération des candidats
    // B5-FIX: au lieu de scanner MAX_BOARD=361 cases, on collecte d'abord les indices
    // avec blocking_weights > 0, puis on itère uniquement sur eux.
    // add_blocking_moves n'attribue du poids qu'aux cases EMPTY proches des menaces,
    // donc blocking_weights > 0 sur ~20-50 cases maximum → ×7-18 plus rapide.
    int hot_indices[MAX_BOARD];
    int hot_count = 0;
    for (int i = 0; i < MAX_BOARD; i++) {
        if (blocking_weights[i] > 0 && g->board[i] == EMPTY)
            hot_indices[hot_count++] = i;
    }

    DefenseCandidate candidates[MAX_BOARD];
    int cand_count = 0;
    
    for (int hi = 0; hi < hot_count; hi++) {
        int i = hot_indices[hi];
        // Rejet immédiat des coups illégaux
        if (is_double_three(g, i, ia_player)) continue;
        
        candidates[cand_count].idx = i;
        candidates[cand_count].blocked_threats = blocking_weights[i];
        
        // Score combiné : Poids du blocage + Potentiel offensif (contre-attaque)
        g->board[i] = ia_player;
        int atk = get_point_score(g, GET_X(i), GET_Y(i), ia_player);
        g->board[i] = EMPTY;
        
        candidates[cand_count].combined_score = (blocking_weights[i] * 1000) + (atk / 100);
        cand_count++;
    }
    
    if (cand_count == 0) return -1; // Aucun blocage légal trouvé
    
    // 3. Tri
    qsort(candidates, cand_count, sizeof(DefenseCandidate), compare_defense);
    
    #ifdef DEBUG
    printf("[CRISIS] threat_space: %d cands, top=(%d,%d)\n", cand_count, 
           GET_X(candidates[0].idx), GET_Y(candidates[0].idx));
    #endif

    // 4. VÉRIFICATION PARANOÏAQUE (La clé du correctif)
    // On ne retourne un coup que s'il empêche VRAIMENT la défaite immédiate
    
    for (int i = 0; i < cand_count; i++) {
        int idx = candidates[i].idx;

        // Refuser les coups illégaux (double-three) avant même d'essayer
        if (is_double_three(g, idx, ia_player)) continue;

        bool is_safe = true;
        
        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);
        
    // CHECK 1 : Est-ce que l'adversaire a ENCORE un coup gagnant immédiat ?
                // threat_counts[IDX_WIN] est mis à jour par apply_move (O(1)).
                // Localiser les coups gagnants via cand_list (voisins des pierres)
                // au lieu de scanner les 361 cases.
                bool still_wins = false;
                if (g->threat_counts[opponent][IDX_WIN] > 0 || g->captures[opponent] >= 4) {
                    for (int ci = 0; ci < g->cand_count; ci++) {
                        int k = g->cand_list[ci];
                        if (is_winning_threat(g, k, opponent)) {
                            still_wins = true;
                            #ifdef DEBUG
                            if (i==0) printf("  reject (%d,%d): opp wins at (%d,%d)\n",
                                             GET_X(idx), GET_Y(idx), GET_X(k), GET_Y(k));
                            #endif
                            break;
                        }
                    }
                }
                is_safe = !still_wins;
        
        undo_move(g, ia_player, &undo);
        
        if (is_safe) {
            #ifdef DEBUG
            printf("  defense ok: (%d,%d)\n", GET_X(idx), GET_Y(idx));
            #endif
            return idx;
        }
    }
    
    // 5. BAROUD D'HONNEUR amélioré
    // Aucun coup parfait trouvé : avant de jouer défensif, chercher une CONTRE-VICTOIRE.
    // Si l'IA peut gagner en 1 ply (alignement de 5 ou 5e capture), le jouer immédiatement
    // — même si l'adversaire a 2+ coups gagnants, une victoire simultanée l'emporte.
    // Scan 1-ply inline (find_immediate_win est static dans ai.c, on reproduit la logique).
    for (int ci = 0; ci < g->cand_count; ci++) {
        int k = g->cand_list[ci];
        if (is_double_three(g, k, ia_player)) continue;
        MoveUndo undo_cw;
        apply_move(g, k, ia_player, &undo_cw);
        bool wins = (g->threat_counts[ia_player][IDX_WIN] > 0 || g->captures[ia_player] >= 5);
        undo_move(g, ia_player, &undo_cw);
        if (wins) {
            #ifdef DEBUG
            printf("  counter-win: (%d,%d)\n", GET_X(k), GET_Y(k));
            #endif
            return k;
        }
    }

    // Pas de contre-victoire : choisir le coup qui laisse le MOINS
    // de coups gagnants immédiats à l'adversaire.
    int best_desperate = candidates[0].idx;
    int min_opp_wins = INT_MAX;
    int desperate_limit = (cand_count < 8) ? cand_count : 8;

    for (int i = 0; i < desperate_limit; i++) {
        int idx = candidates[i].idx;
        if (is_double_three(g, idx, ia_player)) continue;

        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);

        // Compter les réponses gagnantes immédiates de l'adversaire
        int opp_wins = 0;
        for (int ci = 0; ci < g->cand_count; ci++) {
            int k = g->cand_list[ci];
            if (is_winning_threat(g, k, opponent)) {
                opp_wins++;
                if (opp_wins >= min_opp_wins) break; // Pas mieux, arrêt prématuré
            }
        }

        undo_move(g, ia_player, &undo);

        if (opp_wins < min_opp_wins) {
            min_opp_wins = opp_wins;
            best_desperate = idx;
            if (opp_wins == 0) break; // Trouvé un coup sans réponse gagnante
        }
    }

    #ifdef DEBUG
    printf("  no perfect defense, desperate=(%d,%d) opp_wins=%d\n",
           GET_X(best_desperate), GET_Y(best_desperate), min_opp_wins);
    #endif

    return best_desperate;
}
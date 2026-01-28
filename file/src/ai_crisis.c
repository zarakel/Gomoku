#include "../include/gomoku.h"

/*
 * ============================================================================
 * CRISIS DETECTION ENGINE
 * ============================================================================
 * Détecte si l'adversaire a des menaces imparables et calcule le niveau de crise.
 * 
 * Crisis Levels:
 * 0 = Pas de crise (jeu normal)
 * 1 = Menace sérieuse (Open 3 ou mieux)
 * 2 = Menace critique (Open 4 ou 2+ Open 3)
 * 3 = Mort imminente (Multiple Open 4 ou VCF détecté)
 */

/*
 * Analyse une position pour détecter si c'est un coup gagnant adverse
 */
bool is_winning_threat(game *g, int idx, int opponent) {
    if (g->board[idx] != EMPTY) return false;
    
    // Test victoire par alignement
    g->board[idx] = opponent;
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
    g->board[idx] = EMPTY;
    
    if (score >= WIN_SCORE) return true;
    
    // Test victoire par capture
    if (g->captures[opponent] >= 4) {
        int caps = count_potential_captures(g, GET_X(idx), GET_Y(idx), opponent);
        if (g->captures[opponent] + (caps / 2) >= 5) return true;
    }
    
    return false;
}

/*
 * Compte les menaces sérieuses de l'adversaire
 */
static int count_immediate_threats(game *g, int opponent, int *threat_moves) {
    int count = 0;
    
    // Bounding box pour optimisation
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty = true;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty = false;
            int cx = GET_X(i); int cy = GET_Y(i);
            if (cx < min_x) min_x = cx; if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy; if (cy > max_y) max_y = cy;
        }
    }
    
    if (empty) return 0;
    
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;
    
    // Scanner les coups adverses possibles
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != EMPTY) continue;
            
            // Vérifier si ce coup crée une menace
            g->board[idx] = opponent;
            int score = get_point_score(g, x, y, opponent);
            g->board[idx] = EMPTY;
            
            // Menace sérieuse : Open 4 ou mieux
            if (score >= OPEN_FOUR) {
                if (count < 10) {
                    threat_moves[count++] = idx;
                }
            }
        }
    }
    
    return count;
}

/*
 * Analyse la situation et met à jour le crisis state
 */
void update_crisis_state(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    // Reset
    g->in_crisis = false;
    g->crisis_level = 0;
    g->crisis_move_count = 0;
    
    // 1. Check victoire immédiate adverse
    int winning_moves = 0;
    for (int i = 0; i < MAX_BOARD && winning_moves < 10; i++) {
        if (is_winning_threat(g, i, opponent)) {
            g->crisis_moves[winning_moves++] = i;
        }
    }
    
    if (winning_moves > 0) {
        g->in_crisis = true;
        g->crisis_move_count = winning_moves;
        
        if (winning_moves == 1) {
            g->crisis_level = 2; // Une seule menace de mort (bloquable)
        } else {
            g->crisis_level = 3; // Plusieurs menaces = mort quasi-certaine
        }
        
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU %d : %d coup(s) gagnant(s) détecté(s) pour l'adversaire\n", 
               g->crisis_level, winning_moves);
        #endif
        return;
    }
    
    // 2. Check menaces fortes (Open 4)
    int open_four_count = count_immediate_threats(g, opponent, g->crisis_moves);
    g->crisis_move_count = open_four_count;
    
    if (open_four_count >= 2) {
        g->in_crisis = true;
        g->crisis_level = 3; // Double Open 4 = perdu
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU 3 : %d Open Four adverses détectés\n", open_four_count);
        #endif
        return;
    }
    
    if (open_four_count == 1) {
        g->in_crisis = true;
        g->crisis_level = 2; // Un Open 4 = très dangereux
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU 2 : Open Four adverse détecté\n");
        #endif
        return;
    }
    
    // 3. Check menaces moyennes (multiple Open 3)
    int open_three_count = g->threat_counts[opponent][IDX_OPEN_THREE];
    
    if (open_three_count >= 2) {
        g->in_crisis = true;
        g->crisis_level = 1; // Double Open 3 = préoccupant
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU 1 : %d Open Three adverses\n", open_three_count);
        #endif
        return;
    }
    
    // 4. Check captures dangereuses
    if (g->captures[opponent] >= 4) {
        int vulnerable = count_vulnerable_pairs(g, ia_player);
        if (vulnerable > 0) {
            g->in_crisis = true;
            g->crisis_level = 2;
            #ifdef DEBUG
            printf(">>> CRISE NIVEAU 2 : Adversaire à 4 captures (%d paires vulnérables)\n", vulnerable);
            #endif
            return;
        }
    }
    
    // Pas de crise détectée
    g->in_crisis = false;
    g->crisis_level = 0;
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
    int blocked_threats;    // Nombre de menaces bloquées
    int attack_score;       // Score offensif du coup
    int defense_score;      // Score défensif du coup
    int combined_score;     // Score total
} DefenseCandidate;

static int compare_defense_candidates(const void *a, const void *b) {
    DefenseCandidate *da = (DefenseCandidate *)a;
    DefenseCandidate *db = (DefenseCandidate *)b;
    return (db->combined_score - da->combined_score); // Décroissant
}

/*
 * Trouve les cases qui bloquent une menace spécifique
 */
static int find_blocking_moves_for_threat(game *g, int threat_idx, int opponent, int *blocking_moves) {
    int count = 0;
    int x = GET_X(threat_idx);
    int y = GET_Y(threat_idx);
    
    // La menace elle-même est toujours un blocage
    blocking_moves[count++] = threat_idx;
    
    // Chercher d'autres points de blocage dans les 4 directions
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int d = 0; d < 4; d++) {
        // Scanner dans les deux sens
        for (int k = -4; k <= 4; k++) {
            if (k == 0) continue;
            
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            
            if (!IS_VALID(nx, ny)) continue;
            
            int idx = GET_INDEX(nx, ny);
            if (g->board[idx] != EMPTY) continue;
            
            // Vérifier si jouer ici bloque la menace
            g->board[idx] = opponent; // Simuler la menace
            int threat_score = get_point_score(g, x, y, opponent);
            g->board[idx] = EMPTY;
            
            // Si la menace disparaît ou diminue fortement
            if (threat_score < OPEN_FOUR) {
                // Éviter les doublons
                bool already_added = false;
                for (int j = 0; j < count; j++) {
                    if (blocking_moves[j] == idx) {
                        already_added = true;
                        break;
                    }
                }
                
                if (!already_added && count < MAX_BOARD) {
                    blocking_moves[count++] = idx;
                }
            }
        }
    }
    
    return count;
}

/*
 * Trouve LE meilleur coup défensif en analysant l'espace des menaces
 */
int find_best_defense_with_threat_space(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    // 1. Identifier toutes les menaces actives
    if (g->crisis_move_count == 0) return -1;
    
    // 2. Pour chaque menace, trouver ses points de blocage
    int blocking_count[MAX_BOARD];
    memset(blocking_count, 0, sizeof(blocking_count));
    
    for (int i = 0; i < g->crisis_move_count; i++) {
        int threat_idx = g->crisis_moves[i];
        int local_blocks[MAX_BOARD];
        int local_count = find_blocking_moves_for_threat(g, threat_idx, opponent, local_blocks);
        
        for (int j = 0; j < local_count; j++) {
            int block_idx = local_blocks[j];
            blocking_count[block_idx]++;
        }
    }
    
    // 3. Construire la liste des candidats défensifs
    DefenseCandidate candidates[MAX_BOARD];
    int candidate_count = 0;
    
    // ===== NOUVEAU : Liste de secours pour les coups légaux =====
    int fallback_moves[MAX_BOARD];
    int fallback_count = 0;
    // ===========================================================
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        if (blocking_count[idx] == 0) continue;
        
        // ===== MODIFICATION : Sauvegarder les coups légaux séparément =====
        bool is_legal = !is_double_three(g, idx, ia_player);
        
        if (!is_legal) {
            #ifdef DEBUG
            printf("    ⚠️ Candidat défensif (%d,%d) rejeté (Double-Three)\n",
                   GET_X(idx), GET_Y(idx));
            #endif
            continue; // On ignore ce coup interdit
        }
        // ==================================================================
        
        DefenseCandidate *cand = &candidates[candidate_count++];
        cand->idx = idx;
        cand->blocked_threats = blocking_count[idx];
        cand->defense_score = blocking_count[idx] * 1000000;
        
        g->board[idx] = ia_player;
        cand->attack_score = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
        g->board[idx] = EMPTY;
        
        cand->combined_score = cand->defense_score + (cand->attack_score / 10);
        
        if (cand->blocked_threats == g->crisis_move_count) {
            cand->combined_score += 10000000;
        }
        
        // ===== NOUVEAU : Ajouter aussi à la liste de secours =====
        fallback_moves[fallback_count++] = idx;
        // ========================================================
    }
    
    // ===== NOUVEAU : Gestion du cas "aucun candidat" =====
    if (candidate_count == 0) {
        #ifdef DEBUG
        printf("    ⚠️ ALERTE : Tous les candidats défensifs créent des Double-Three !\n");
        printf("    🔍 Recherche d'un coup légal de secours...\n");
        #endif
        
        // Chercher un coup qui nous donne une victoire PLUS RAPIDE
        for (int idx = 0; idx < MAX_BOARD; idx++) {
            if (g->board[idx] != EMPTY) continue;
            if (is_double_three(g, idx, ia_player)) continue;
            
            g->board[idx] = ia_player;
            int our_score = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
            
            // Si on crée un Open 4 ou mieux, on peut gagner avant eux
            if (our_score >= OPEN_FOUR) {
                g->board[idx] = EMPTY;
                
                #ifdef DEBUG
                printf("    ✅ CONTRE-ATTAQUE trouvée : (%d,%d) (score: %d)\n",
                    GET_X(idx), GET_Y(idx), our_score);
                #endif
                return idx;
            }
            
            g->board[idx] = EMPTY;
        }
        
        // Fallback 1 : Chercher N'IMPORTE QUEL coup légal qui réduit la menace
        for (int idx = 0; idx < MAX_BOARD; idx++) {
            if (g->board[idx] != EMPTY) continue;
            if (is_double_three(g, idx, ia_player)) continue;
            
            // Tester si ce coup a un intérêt défensif quelconque
            g->board[idx] = ia_player;
            
            // Recalculer les menaces adverses après notre coup
            int remaining_threats = 0;
            for (int i = 0; i < MAX_BOARD; i++) {
                if (is_winning_threat(g, i, opponent)) {
                    remaining_threats++;
                }
            }
            
            g->board[idx] = EMPTY;
            
            // Si ce coup réduit les menaces, on le prend
            if (remaining_threats < g->crisis_move_count) {
                return idx;
            }
        }
        
        // Fallback 2 : Si vraiment RIEN ne marche, jouer le coup le mieux noté heuristiquement
        #ifdef DEBUG
        printf("    ⚠️ Aucun coup défensif efficace trouvé. Utilisation de l'heuristique...\n");
        #endif
        
        MoveCandidate emergency_moves[MAX_BOARD];
        int emergency_count = generate_moves(g, emergency_moves, ia_player, 0, -1);
        
        for (int i = 0; i < emergency_count; i++) {
            if (!is_double_three(g, emergency_moves[i].index, ia_player)) {
                #ifdef DEBUG
                printf("    🆘 Coup d'urgence : (%d,%d) (score: %d)\n",
                       GET_X(emergency_moves[i].index), GET_Y(emergency_moves[i].index),
                       emergency_moves[i].score_estim);
                #endif
                return emergency_moves[i].index;
            }
        }
        
        // Fallback 3 : Désespoir total — jouer n'importe où
        #ifdef DEBUG
        printf("    💀 SITUATION DÉSESPÉRÉE : Aucun coup légal efficace. Coup aléatoire.\n");
        #endif
        
        for (int idx = 0; idx < MAX_BOARD; idx++) {
            if (g->board[idx] == EMPTY && !is_double_three(g, idx, ia_player)) {
                return idx;
            }
        }
        
        return -1; // Vraiment aucun coup possible (partie perdue)
    }
    // =======================================================
    
    // 4. Trier par score combiné
    qsort(candidates, candidate_count, sizeof(DefenseCandidate), compare_defense_candidates);
    
    #ifdef DEBUG
    printf(">>> THREAT SPACE : %d candidats défensifs trouvés\n", candidate_count);
    printf("    Meilleur : (%d,%d) bloque %d menaces (score: %d)\n",
           GET_X(candidates[0].idx), GET_Y(candidates[0].idx),
           candidates[0].blocked_threats, candidates[0].combined_score);
    #endif
    
    // 5. Vérifier que le meilleur coup ne laisse pas une mort immédiate
    int best_idx = candidates[0].idx;
    
    MoveUndo undo;
    apply_move(g, best_idx, ia_player, &undo);
    
    bool still_losing = false;
    for (int i = 0; i < MAX_BOARD && !still_losing; i++) {
        if (is_winning_threat(g, i, opponent)) {
            still_losing = true;
        }
    }
    
    undo_move(g, ia_player, &undo);
    
    if (still_losing && candidate_count > 1) {
        #ifdef DEBUG
        printf("    Premier choix insuffisant, test du 2ème candidat\n");
        #endif
        return candidates[1].idx;
    }
    
    return best_idx;
}
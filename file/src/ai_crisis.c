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
 * Vérifie si 'idx' est un coup gagnant immédiat pour 'opponent'
 */
bool is_winning_threat(game *g, int idx, int opponent) {
    if (g->board[idx] != EMPTY) return false;
    
    // 1. Victoire par alignement (5)
    g->board[idx] = opponent;
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
    g->board[idx] = EMPTY;
    
    if (score >= WIN_SCORE) return true;
    
    // 2. Victoire par capture (si règle active)
    if (g->captures[opponent] >= 4) {
        int caps = count_potential_captures(g, GET_X(idx), GET_Y(idx), opponent);
        if (g->captures[opponent] + (caps / 2) >= 5) return true; // (caps/2 car count retourne le nombre de pierres)
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
    
    g->in_crisis = false;
    g->crisis_level = 0;
    g->crisis_move_count = 0;
    
    // 1. VICTOIRE IMMÉDIATE ADVERSE (Urgence absolue)
    int winning_moves = 0;
    
    // Optimisation : Scanner uniquement les cases autour des pierres existantes ?
    // Pour la sûreté, on scanne tout le board ici (c'est rapide, juste un check)
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] == EMPTY && is_winning_threat(g, i, opponent)) {
            g->crisis_moves[winning_moves++] = i;
            if (winning_moves >= 10) break;
        }
    }
    
    if (winning_moves > 0) {
        g->in_crisis = true;
        g->crisis_move_count = winning_moves;
        g->crisis_level = (winning_moves == 1) ? 2 : 3; // 3 = Mort quasi certaine (double menace)
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU %d : %d coup(s) gagnant(s) détecté(s) !\n", g->crisis_level, winning_moves);
        #endif
        return;
    }
    
    // 2. Check menaces fortes (Open 4)
    int open_four_count = count_immediate_threats(g, opponent, g->crisis_moves);
    g->crisis_move_count = open_four_count;
    
    if (open_four_count > 0) {
        g->in_crisis = true;
        g->crisis_move_count = open_four_count;
        g->crisis_level = (open_four_count >= 2) ? 3 : 2;
        #ifdef DEBUG
        printf(">>> CRISE NIVEAU %d : %d Open Four adverses détectés\n", g->crisis_level, open_four_count);
        #endif
        return;
    }
    
    // 3. Check menaces moyennes (Open 3) -> AJOUT CRITIQUE ICI
    // Si aucune menace mortelle, on cherche les Open 3 pour les ajouter à la liste de crise
    int open_three_count = g->threat_counts[opponent][IDX_OPEN_THREE];
    
    if (open_three_count > 0) {
        // On doit scanner le plateau pour TROUVER où sont ces Open 3
        int count_found = 0;
        
        // Scan rapide pour localiser les menaces
        // (On réutilise la logique de count_immediate_threats mais avec un seuil plus bas)
        int min_x = 0, max_x = BOARD_SIZE - 1, min_y = 0, max_y = BOARD_SIZE - 1; // Optimiser si besoin
        
        for (int y = min_y; y <= max_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                int idx = GET_INDEX(x, y);
                if (g->board[idx] != EMPTY) continue;
                
                g->board[idx] = opponent;
                int score = get_point_score(g, x, y, opponent);
                g->board[idx] = EMPTY;
                
                // Si ce coup crée un Open 4, c'est qu'il complète un Open 3 existant !
                // C'est donc le point vital à bloquer.
                if (score >= OPEN_FOUR) {
                    if (count_found < 10) {
                        g->crisis_moves[count_found++] = idx;
                    }
                }
            }
        }
        
        if (count_found > 0) {
            g->crisis_move_count = count_found;
            g->in_crisis = true;
            g->crisis_level = 1; // Niveau prudence
            if (count_found >= 2) g->crisis_level = 2; // Double menace = Danger
            
            #ifdef DEBUG
            printf(">>> CRISE NIVEAU %d : %d Open Three adverses localisés\n", g->crisis_level, count_found);
            #endif
            return;
        }
    }
    
    // 4. Check captures (Code existant inchangé...)
    if (g->captures[opponent] >= 4) {
        int vulnerable = count_vulnerable_pairs(g, ia_player);
        if (vulnerable > 0) {
            g->in_crisis = true;
            g->crisis_level = 2;
            #ifdef DEBUG
            printf(">>> CRISE NIVEAU 2 : Adversaire à 4 captures\n");
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
    int x = GET_X(threat_idx);
    int y = GET_Y(threat_idx);
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    // La case de menace elle-même est le meilleur blocage
    counts[threat_idx] += 10; 
    
    for (int d = 0; d < 4; d++) {
        for (int k = -4; k <= 4; k++) {
            if (k == 0) continue;
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == EMPTY) {
                int idx = GET_INDEX(nx, ny);
                
                // Simulation : Si on joue là, est-ce que la menace diminue ?
                g->board[idx] = opponent; // L'adversaire joue son coup menaçant
                int score_before = get_point_score(g, x, y, opponent);
                g->board[idx] = ia_player; // NOUS jouons le blocage
                
                // On vérifie le score de l'adversaire SI on bloque ici
                // Attention: pour tester si ça bloque, on remet EMPTY et on re-test la menace ?
                // Non, plus simple : on joue notre pierre. Est-ce que l'adversaire peut toujours gagner en (x,y) ?
                
                g->board[idx] = ia_player;
                g->board[threat_idx] = opponent;
                int score_after = get_point_score(g, x, y, opponent);
                
                // Si le score de la menace chute significativement
                if (score_after < OPEN_FOUR && score_before >= OPEN_FOUR) {
                    counts[idx] += 2; // Bon blocage
                }
                
                // Reset
                g->board[threat_idx] = EMPTY;
                g->board[idx] = EMPTY;
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
    
    // 2. Génération des candidats
    DefenseCandidate candidates[MAX_BOARD];
    int cand_count = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] == EMPTY && blocking_weights[i] > 0) {
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
    }
    
    if (cand_count == 0) return -1; // Aucun blocage légal trouvé
    
    // 3. Tri
    qsort(candidates, cand_count, sizeof(DefenseCandidate), compare_defense);
    
    #ifdef DEBUG
    printf(">>> THREAT SPACE : %d candidats. Top: (%d,%d)\n", cand_count, 
           GET_X(candidates[0].idx), GET_Y(candidates[0].idx));
    #endif

    // 4. VÉRIFICATION PARANOÏAQUE (La clé du correctif)
    // On ne retourne un coup que s'il empêche VRAIMENT la défaite immédiate
    
    for (int i = 0; i < cand_count; i++) {
        int idx = candidates[i].idx;
        bool is_safe = true;
        
        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);
        
        // CHECK 1 : Est-ce que l'adversaire a ENCORE un coup gagnant immédiat (5 alignés) ?
        for (int k = 0; k < MAX_BOARD; k++) {
            // Optimisation : On pourrait scanner seulement les cases vides voisines des pierres adverses
            if (g->board[k] == EMPTY) {
                if (is_winning_threat(g, k, opponent)) {
                    is_safe = false;
                    #ifdef DEBUG
                    if (i==0) printf("    ❌ Refus (%d,%d) : L'adversaire gagne encore en (%d,%d)\n", 
                                     GET_X(idx), GET_Y(idx), GET_X(k), GET_Y(k));
                    #endif
                    break;
                }
            }
        }
        
        // CHECK 2 : (Si on n'est pas déjà mort) Est-ce qu'on lui laisse un Open 4 imparable ?
        // Si on bloque un 4, mais qu'il en a un autre ailleurs, on a perdu quand même.
        if (is_safe && g->crisis_level >= 2) {
            int open_four_remaining = 0;
             for (int k = 0; k < MAX_BOARD; k++) {
                 if (g->board[k] == EMPTY) {
                     g->board[k] = opponent;
                     if (get_point_score(g, GET_X(k), GET_Y(k), opponent) >= OPEN_FOUR) {
                         open_four_remaining++;
                     }
                     g->board[k] = EMPTY;
                     if (open_four_remaining > 0) break;
                 }
             }
             if (open_four_remaining > 0) {
                 is_safe = false; // Ce coup ne nous sauve pas complètement
             }
        }

        undo_move(g, ia_player, &undo);
        
        if (is_safe) {
            #ifdef DEBUG
            printf("    ✅ DÉFENSE VALIDÉE : (%d,%d)\n", GET_X(idx), GET_Y(idx));
            #endif
            return idx;
        }
    }
    
    // 5. BAROUD D'HONNEUR
    // Si aucun coup ne nous sauve parfaitement, on joue le meilleur candidat heuristique
    // (Peut-être que l'adversaire ne verra pas sa victoire)
    #ifdef DEBUG
    printf("    ⚠️ ECHEC DÉFENSE PARFAITE. Tentative désespérée en (%d,%d)\n", 
           GET_X(candidates[0].idx), GET_Y(candidates[0].idx));
    #endif
    
    return candidates[0].idx;
}
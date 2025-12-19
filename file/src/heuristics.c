#include "../include/gomoku.h"

// Helper inline pour éviter la répétition de code
static inline int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
    int score = 0;
    int len = 1;
    
    bool start_open = false; 
    bool end_open = false;   
    
    // --- 1. SCAN POSITIF ---
    int i = 1;
    for (; i < 5; i++) {
        int nx = x + dx * i;
        int ny = y + dy * i;
        if (!IS_VALID(nx, ny)) break;
        int cell = g->board[GET_INDEX(nx, ny)]; 
        if (cell == player) len++;
        else if (cell == EMPTY) { end_open = true; break; } 
        else break; 
    }

    // --- 2. SCAN NÉGATIF ---
    int j = 1;
    for (; j < 5; j++) {
        int nx = x - dx * j;
        int ny = y - dy * j;
        if (!IS_VALID(nx, ny)) break;
        int cell = g->board[GET_INDEX(nx, ny)];
        if (cell == player) len++;
        else if (cell == EMPTY) { start_open = true; break; }
        else break;
    }

    int open_ends = (start_open ? 1 : 0) + (end_open ? 1 : 0);

    // --- 3. SCORING CONTINU (Classique) ---
    if (len >= 5) return WIN_SCORE;
    if (len == 4) {
        if (open_ends == 2) score = OPEN_FOUR;      
        else if (open_ends == 1) score = CLOSED_FOUR; 
        else score = 100000; // Blocked 4 (Mort)
    }
    else if (len == 3) {
        if (open_ends == 2) score = OPEN_THREE;     // 10M (CRITIQUE)
        else if (open_ends == 1) score = CLOSED_THREE; // 3M (Sérieux)
    }
    else if (len == 2) {
        if (open_ends == 2) score = OPEN_TWO; 
        else if (open_ends == 1) score = CLOSED_TWO;
    }

    // --- 4. DÉTECTION AVANCÉE DES TROUS ---
    
    if (len < 4) {
        // --- Gap Positif ---
        if (end_open) {
             int px = x + dx * i; 
             int py = y + dy * i;
             
             int extra_len = 0;
             int start_k = 1;
             bool double_gap = false;

             // Vérification Double Gap (_ _)
             int ppx1 = px + dx;
             int ppy1 = py + dy;
             if (IS_VALID(ppx1, ppy1) && g->board[ppy1*BOARD_SIZE+ppx1] == EMPTY) {
                 double_gap = true;
                 start_k = 2; 
             }

             int last_k = start_k;
             for (int k = start_k; k <= 4; k++) {
                 int ppx = px + dx * k;
                 int ppy = py + dy * k;
                 if (!IS_VALID(ppx, ppy) || g->board[ppy*BOARD_SIZE+ppx] != player) break;
                 extra_len++;
                 last_k = k;
             }

             if (extra_len > 0) {
                 int virtual_len = len + extra_len;
                 
                 bool far_end_open = false;
                 int far_x = px + dx * (last_k + 1);
                 int far_y = py + dy * (last_k + 1);
                 if (IS_VALID(far_x, far_y) && g->board[far_y*BOARD_SIZE+far_x] == EMPTY) {
                     far_end_open = true;
                 }

                 if (double_gap) {
                     if (virtual_len >= 4) score = CLOSED_FOUR; 
                     else if (virtual_len == 3) {
                         if (start_open && far_end_open) score = OPEN_THREE; 
                         else score = CLOSED_THREE; 
                     }
                 } else {
                     if (virtual_len >= 5) score = WIN_SCORE; 
                     
                     else if (virtual_len == 4) {
                         // CORRECTION : Gapped Open 4 = OPEN_FOUR (pas WIN_SCORE)
                         if (start_open && far_end_open) score = OPEN_FOUR; 
                         else score = CLOSED_FOUR; 
                     }
                     
                     else if (virtual_len == 3) {
                         if (start_open && far_end_open) score = OPEN_THREE; 
                         else score = CLOSED_THREE; 
                     }
                 }
             }
        }

        // --- Gap Négatif ---
        if (start_open) {
             int mx = x - dx * j;
             int my = y - dy * j;
             
             int extra_len = 0;
             int start_k = 1;
             bool double_gap = false;

             int mmx1 = mx - dx;
             int mmy1 = my - dy;
             if (IS_VALID(mmx1, mmy1) && g->board[mmy1*BOARD_SIZE+mmx1] == EMPTY) {
                 double_gap = true;
                 start_k = 2;
             }

             int last_k = start_k;
             for (int k = start_k; k <= 4; k++) {
                 int mmx = mx - dx * k;
                 int mmy = my - dy * k;
                 if (!IS_VALID(mmx, mmy) || g->board[mmy*BOARD_SIZE+mmx] != player) break;
                 extra_len++;
                 last_k = k;
             }

             if (extra_len > 0) {
                 int virtual_len = len + extra_len;
                 
                 bool far_end_open = false;
                 int far_x = mx - dx * (last_k + 1);
                 int far_y = my - dy * (last_k + 1);
                 if (IS_VALID(far_x, far_y) && g->board[far_y*BOARD_SIZE+far_x] == EMPTY) {
                     far_end_open = true;
                 }

                 if (double_gap) {
                     if (virtual_len >= 4) score = CLOSED_FOUR;
                     else if (virtual_len == 3) {
                         if (end_open && far_end_open) score = OPEN_THREE;
                         else score = CLOSED_THREE;
                     }
                 } else {
                     if (virtual_len >= 5) score = WIN_SCORE;
                     
                     else if (virtual_len == 4) {
                         // CORRECTION : Gapped Open 4 = OPEN_FOUR
                         if (end_open && far_end_open) score = OPEN_FOUR;
                         else score = CLOSED_FOUR; 
                     }
                     
                     else if (virtual_len == 3) {
                         if (end_open && far_end_open) score = OPEN_THREE; 
                         else score = CLOSED_THREE;
                     }
                 }
             }
        }
    }
    return score;
}

/* Fonction d'évaluation "Rayon X" */
int get_point_score(game *g, int x, int y, int player) {
    int total_score = 0;
    total_score += evaluate_line(g, x, y, 1, 0, player);  
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    total_score += evaluate_line(g, x, y, 0, 1, player);  
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    total_score += evaluate_line(g, x, y, 1, 1, player);  
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    total_score += evaluate_line(g, x, y, 1, -1, player); 
    return total_score;
}

// Helper pour recalculer les scores globaux ET repérer la menace maximale unique
static void recalculate_scores(game *g, int *score_p1, int *score_p2, int *max_threat_p1, int *max_threat_p2) {
    *score_p1 = 0; *score_p2 = 0;
    *max_threat_p1 = 0; *max_threat_p2 = 0;
    
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int idx = GET_INDEX(x, y);
            int player = g->board[idx];
            
            if (player == EMPTY) continue;

            int *target_score = (player == P1) ? score_p1 : score_p2;
            int *target_max = (player == P1) ? max_threat_p1 : max_threat_p2;
            int val = 0;

            // On ne lance l'évaluation que si cette pierre est le DÉBUT d'une ligne
            
            // 1. Horizontal
            if (x == 0 || g->board[idx - 1] != player) {
                val = evaluate_line(g, x, y, 1, 0, player);
                *target_score += val;
                if (val > *target_max) *target_max = val;
            }

            // 2. Vertical
            if (y == 0 || g->board[idx - BOARD_SIZE] != player) {
                val = evaluate_line(g, x, y, 0, 1, player);
                *target_score += val;
                if (val > *target_max) *target_max = val;
            }

            // 3. Diag Bas-Droite
            if (x == 0 || y == 0 || g->board[idx - BOARD_SIZE - 1] != player) {
                val = evaluate_line(g, x, y, 1, 1, player);
                *target_score += val;
                if (val > *target_max) *target_max = val;
            }

            // 4. Diag Haut-Droite
            if (x == 0 || y == BOARD_SIZE - 1 || g->board[idx + BOARD_SIZE - 1] != player) {
                val = evaluate_line(g, x, y, 1, -1, player);
                *target_score += val;
                if (val > *target_max) *target_max = val;
            }
        }
    }
}

int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    int current_p1_score = 0;
    int current_p2_score = 0;
    int max_p1_threat = 0;
    int max_p2_threat = 0;

    recalculate_scores(g, &current_p1_score, &current_p2_score, &max_p1_threat, &max_p2_threat);

    int my_score = (player == P1) ? current_p1_score : current_p2_score;
    int opp_score = (player == P1) ? current_p2_score : current_p1_score;
    
    int opp_max_threat = (player == P1) ? max_p2_threat : max_p1_threat;
    int my_max_threat = (player == P1) ? max_p1_threat : max_p2_threat;

    // 1. Victoire/Défaite absolue
    if (opp_max_threat >= WIN_SCORE || g->captures[opponent] >= 5) return -WIN_SCORE; 
    if (my_max_threat >= WIN_SCORE || g->captures[player] >= 5) return WIN_SCORE;

    // 2. Score de base
    long long final_score = (long long)my_score - (long long)opp_score;
    
    // 3. Panique Proportionnelle (simplifiée avec la nouvelle échelle)
    // La menace adverse est multipliée par un facteur selon son niveau
    if (opp_max_threat >= OPEN_THREE) {
        // Menace critique : je dois réagir sauf si j'ai mieux
        if (my_max_threat < CLOSED_FOUR) {
            // Pénalité = menace * 3 (pour forcer la défense)
            final_score -= (long long)(opp_max_threat * 3);
        } else {
            // Course de vitesse : pénalité standard
            final_score -= (long long)(opp_max_threat);
        }
    } else if (opp_max_threat >= CLOSED_THREE) {
        // Menace sérieuse mais pas urgente
        final_score -= (long long)(opp_max_threat * 2);
    }

    // 4. Captures (valent ~0.5 Open Three chacune)
    final_score += (g->captures[player] * CAPTURE_BONUS); 
    final_score -= (g->captures[opponent] * CAPTURE_BONUS);

    // 5. Clamp
    if (final_score > WIN_SCORE - 1) final_score = WIN_SCORE - 1;
    if (final_score < -WIN_SCORE + 1) final_score = -WIN_SCORE + 1;

    return (int)final_score;
}

/* 
 * Vérifie si un coup en (x,y) CRÉE un "Free Three" dans la direction (dx, dy).
 * 
 * RÈGLE CRITIQUE : Le coup doit faire PARTIE du Free Three créé.
 * Si le Free Three existait déjà sans ce coup, il ne compte pas.
 * 
 * Patterns reconnus (le 'X' marqué '*' est le coup joué) :
 *  1.  . X X * .  ou  . X * X .  ou  . * X X .
 *  2.  . X . * X .  ou  . * . X X .  etc.
 */
int check_free_three_pattern(game *g, int x, int y, int dx, int dy, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // Construction d'un mini-buffer de la ligne
    // On scanne 6 cases de chaque côté (indices -6 à +6, total 13 cases)
    int line[13];
    int center = 6;
    
    for (int k = -6; k <= 6; k++) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        if (IS_VALID(nx, ny)) {
            line[center + k] = g->board[ny * BOARD_SIZE + nx];
        } else {
            line[center + k] = opponent; // Hors plateau = bloqué
        }
    }

    // IMPORTANT : On simule la pose de la pierre dans le buffer (pas sur le vrai plateau)
    line[center] = player;

    // --- Recherche de Free Three dont le coup fait PARTIE ---
    
    // Pattern 1 : . X X X . (3 consécutives, fenêtre de 5)
    // Le coup (center) doit être l'un des 3 X
    for (int start = 0; start <= 8; start++) {
        int end = start + 4; // Fenêtre [start, start+4]
        
        // Le coup doit être DANS la fenêtre des 3 pierres (pas sur les bords vides)
        if (center < start + 1 || center > start + 3) continue;
        
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == player &&
            line[start + 3] == player &&
            line[start + 4] == EMPTY) {
            
            // Vérifier qu'il n'y a pas de 4ème pierre adjacente (sinon c'est un Four, pas un Three)
            bool is_four = false;
            if (start > 0 && line[start - 1] == player) is_four = true;
            if (end + 1 < 13 && line[end + 1] == player) is_four = true;
            
            if (!is_four) {
                return 1; // Free Three trouvé, et le coup en fait partie
            }
        }
    }

    // Pattern 2 : . X X . X . (trou au milieu-droite, fenêtre de 6)
    // Pierres aux positions : start+1, start+2, start+4
    for (int start = 0; start <= 7; start++) {
        // Le coup doit être l'une des 3 pierres
        bool coup_in_pattern = (center == start + 1 || center == start + 2 || center == start + 4);
        if (!coup_in_pattern) continue;
        
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == player &&
            line[start + 3] == EMPTY &&
            line[start + 4] == player &&
            line[start + 5] == EMPTY) {
            return 1;
        }
    }

    // Pattern 3 : . X . X X . (trou au milieu-gauche, fenêtre de 6)
    // Pierres aux positions : start+1, start+3, start+4
    for (int start = 0; start <= 7; start++) {
        bool coup_in_pattern = (center == start + 1 || center == start + 3 || center == start + 4);
        if (!coup_in_pattern) continue;
        
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == EMPTY &&
            line[start + 3] == player &&
            line[start + 4] == player &&
            line[start + 5] == EMPTY) {
            return 1;
        }
    }

    return 0;
}

/* Vérifie la règle du Double-Three */
bool is_double_three(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    // Vérifier que la case est vide
    if (g->board[idx] != EMPTY) return false;

    int free_threes = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            free_threes++;
            if (free_threes >= 2) return true; // Optimisation : on arrête dès qu'on en a 2
        }
    }
    
    return false;
}

void explain_double_three(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    printf("--- ANALYSE DOUBLE-THREE (%d, %d) ---\n", x, y);
    
    int free_threes = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    char *dir_names[] = {"Horizontal", "Vertical", "Diag Bas-Droite", "Diag Haut-Droite"};

    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            printf("  > Free Three CRÉÉ par ce coup : %s\n", dir_names[d]);
            free_threes++;
        }
    }
    
    if (free_threes >= 2) {
        printf("  => COUP INTERDIT (Crée %d Free Threes simultanés)\n", free_threes);
    } else {
        printf("  => Coup autorisé (Crée %d Free Three)\n", free_threes);
    }
    printf("---------------------------------------\n");
}
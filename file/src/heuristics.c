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

    // 1. Défaite absolue ?
    if (opp_max_threat >= WIN_SCORE || g->captures[opponent] >= 5) return -WIN_SCORE; 

    // 2. Victoire absolue ?
    if (my_max_threat >= WIN_SCORE || g->captures[player] >= 5) return WIN_SCORE;

    // 3. Score Relatif
    long long final_score = (long long)my_score - (long long)opp_score;
    
    // --- PANIQUE PROPORTIONNELLE INTELLIGENTE ---
    // CORRECTION : Gestion du Closed Three vs Open Three
    
    long long panic_penalty = 0;

    if (opp_max_threat >= CLOSED_THREE) {
        // Si l'adversaire a une menace sérieuse (Closed 3 ou mieux)...
        
        // ... Et que je n'ai pas une victoire quasi-immédiate (Open 4 ou Win) pour contrer...
        if (my_max_threat < OPEN_FOUR) {
            // ALORS : La menace adverse est prioritaire.
            // Je la multiplie par 4.
            // Exemple : Closed 3 (4M) * 4 = 16M de pénalité.
            // Mon Open 3 (10M) ne suffit plus (10M - 16M = -6M). Je dois défendre.
            panic_penalty = (long long)(opp_max_threat * 4);
        } else {
            // SINON : C'est une course de vitesse (j'ai un Open 4).
            // Je garde une pression standard (x2) pour privilégier mon attaque si elle est plus rapide.
            panic_penalty = (long long)(opp_max_threat * 2);
        }
    } else {
        // Menaces faibles (Open 2, etc.), pression standard
        panic_penalty = (long long)(opp_max_threat * 2);
    }

    final_score -= panic_penalty;

    // Bonus/Malus Captures
    final_score += (g->captures[player] * 200000); 
    final_score -= (g->captures[opponent] * 200000);

    // Clamp pour rester dans int
    if (final_score > 2000000000) final_score = 2000000000;
    if (final_score < -2000000000) final_score = -2000000000;

    return (int)final_score;
}

/* 
 * Vérifie si un coup en (x,y) crée un "Free Three" strict dans la direction (dx, dy).
 * Patterns reconnus :
 *  1.  . X X X .  (Standard)
 *  2.  . X _ X X . (Gap)
 *  3.  . X X _ X . (Gap)
 *  4.  . X X _ _ X . (Double Gap - AJOUTÉ)
 */
int check_free_three_pattern(game *g, int x, int y, int dx, int dy, int player) {
    // On simule la pose de la pierre
    g->board[y * BOARD_SIZE + x] = player;

    // On scanne une fenêtre de 9 cases autour du point (x,y) : [-4, +4]
    // On cherche les motifs .XXX. ou .X.XX. ou .XX.X. ou .XX..X.
    
    // Construction d'un mini-buffer de la ligne pour simplifier l'analyse
    int line[11]; // Indices 0 à 10. Le coup joué est au centre (index 5)
    int center = 5;
    
    for (int k = -5; k <= 5; k++) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        if (!IS_VALID(nx, ny)) line[center + k] = -1; // Bord
        else line[center + k] = g->board[ny * BOARD_SIZE + nx];
    }

    // On remet la case vide pour ne pas corrompre le plateau
    g->board[y * BOARD_SIZE + x] = EMPTY;

    // Analyse des motifs
    
    // Pattern 1 : . X X X . (Continu)
    for (int k = -2; k <= 0; k++) { 
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == player && 
            line[center+k+3] == player && 
            line[center+k+4] == EMPTY) return 1;
    }

    // Pattern 2 : . X _ X X . (Gap)
    for (int k = -3; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == EMPTY && 
            line[center+k+3] == player && 
            line[center+k+4] == player && 
            line[center+k+5] == EMPTY) return 1;
    }

    // Pattern 3 : . X X _ X . (Gap inverse)
    for (int k = -3; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == player && 
            line[center+k+3] == EMPTY && 
            line[center+k+4] == player && 
            line[center+k+5] == EMPTY) return 1;
    }

    // Pattern 4 : . X X _ _ X . (Double Gap) - AJOUT CRITIQUE
    // Fenêtre de 7 cases : [EMPTY, P, P, EMPTY, EMPTY, P, EMPTY]
    // Le coup joué peut être n'importe lequel des P.
    for (int k = -4; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == player && 
            line[center+k+3] == EMPTY && 
            line[center+k+4] == EMPTY && 
            line[center+k+5] == player && 
            line[center+k+6] == EMPTY) return 1;
    }
    
    // Pattern 4b : . X _ _ X X . (Double Gap Inverse)
    for (int k = -4; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == EMPTY && 
            line[center+k+3] == EMPTY && 
            line[center+k+4] == player && 
            line[center+k+5] == player && 
            line[center+k+6] == EMPTY) return 1;
    }

    return 0;
}

/* Vérifie la règle du Double-Three */
bool is_double_three(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    // Optimisation : Si la case a des voisins ennemis trop proches qui bloquent tout, inutile de vérifier
    // (Optionnel, on garde la logique brute pour la précision)

    int free_threes = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            free_threes++;
        }
    }
    
    // Règle : Interdit si >= 2 Free Threes simultanés
    return (free_threes >= 2);
}

void explain_double_three(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    g->board[idx] = player; 
    int free_threes = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    char *dir_names[] = {"Horizontal", "Vertical", "Diag Bas-Droite", "Diag Haut-Droite"};

    printf("--- ANALYSE DOUBLE-THREE (%d, %d) ---\n", x, y);
    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            printf("  > Free Three détecté : %s\n", dir_names[d]);
            free_threes++;
        }
    }
    if (free_threes >= 2) printf("  => COUP INTERDIT (Total: %d)\n", free_threes);
    else printf("  => Coup Légal (Total: %d)\n", free_threes);
    printf("---------------------------------------\n");

    g->board[idx] = EMPTY; 
}
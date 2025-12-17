#include "../include/gomoku.h"

// Helper inline pour éviter la répétition de code
static inline int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
    int score = 0;
    int len = 1;
    int open_ends = 0;
    
    // --- 1. SCAN POSITIF (Direction +) ---
    int i = 1;
    for (; i < 5; i++) {
        int nx = x + dx * i;
        int ny = y + dy * i;
        
        // Utilisation de la macro pour la sécurité (évite les erreurs manuelles)
        if (!IS_VALID(nx, ny)) break;
        
        int cell = g->board[GET_INDEX(nx, ny)]; 
        if (cell == player) len++;
        else if (cell == EMPTY) { open_ends++; break; } 
        else break; 
    }

    // --- 2. SCAN NÉGATIF (Direction -) ---
    int j = 1;
    for (; j < 5; j++) {
        int nx = x - dx * j;
        int ny = y - dy * j;
        
        if (!IS_VALID(nx, ny)) break;
        
        int cell = g->board[GET_INDEX(nx, ny)];
        if (cell == player) len++;
        else if (cell == EMPTY) { open_ends++; break; }
        else break;
    }

    // --- 3. SCORING CONTINU (Classique) ---
    if (len >= 5) return WIN_SCORE;
    if (len == 4) {
        if (open_ends >= 1) score += 20000000; // Open 4 (Win imparable)
        else score += 250000; // Closed 4
    }
    else if (len == 3) {
        // Open 3 boosté pour éviter les fourchettes
        if (open_ends == 2) score += 140000; 
        else if (open_ends == 1) score += 95000; // Closed 3
    }
    else if (len == 2) {
        if (open_ends == 2) score += 5000; 
    }

    // --- 4. DÉTECTION AVANCÉE DES TROUS (SINGLE & DOUBLE GAPS) ---
    
    if (len < 4) {
        // --- Gap Positif ---
        int px = x + dx * i; 
        int py = y + dy * i;
        
        if (IS_VALID(px, py) && g->board[py*BOARD_SIZE+px] == EMPTY) {
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

             // Scan des pierres après le(s) trou(s)
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
                 
                 // Vérification : Est-ce que le bout LOINTAIN est ouvert ?
                 // (Pour distinguer un Gapped Open 3 d'un Gapped Closed 3)
                 bool far_end_open = false;
                 int far_x = px + dx * (last_k + 1);
                 int far_y = py + dy * (last_k + 1);
                 if (IS_VALID(far_x, far_y) && g->board[far_y*BOARD_SIZE+far_x] == EMPTY) {
                     far_end_open = true;
                 }

                 if (double_gap) {
                     // Menace Double Gap (X X _ _ X)
                     if (virtual_len >= 4) score += 20000000; // Quasi Open 4
                     else if (virtual_len == 3) {
                         if (open_ends > 0 && far_end_open) score += 130000; // Double Gap Open 3
                         else score += 95000; 
                     }
                 } else {
                     // Menace Single Gap (X X _ X)
                     if (virtual_len >= 5) score += WIN_SCORE; // Win virtuel
                     
                     // CORRECTION CRITIQUE : Gapped 4 (XXX_X) = Open 4 (20M)
                     // Car le trou est un point de victoire immédiat.
                     else if (virtual_len == 4) score += 20000000; 
                     
                     else if (virtual_len == 3) {
                         // Gapped Open 3 (.XX.X.) = Open 3 (.XXX.)
                         // Si ouvert au début (open_ends > 0) ET ouvert à la fin (far_end_open)
                         if (open_ends > 0 && far_end_open) score += 140000; 
                         else score += 95000; // Gapped Closed 3
                     }
                 }
             }
        }

        // --- Gap Négatif (Même logique) ---
        int mx = x - dx * j;
        int my = y - dy * j;
        
        if (IS_VALID(mx, my) && g->board[my*BOARD_SIZE+mx] == EMPTY) {
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
                     if (virtual_len >= 4) score += 20000000;
                     else if (virtual_len == 3) {
                         if (open_ends > 0 && far_end_open) score += 130000;
                         else score += 95000;
                     }
                 } else {
                     if (virtual_len >= 5) score += WIN_SCORE;
                     else if (virtual_len == 4) score += 20000000; // Gapped 4 = Open 4
                     else if (virtual_len == 3) {
                         if (open_ends > 0 && far_end_open) score += 140000; // Gapped Open 3 = Open 3
                         else score += 95000;
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

int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // 1. Défaite absolue ?
    if (g->score[opponent] >= WIN_SCORE || g->captures[opponent] >= 5) {
        return -WIN_SCORE; 
    }

    // 2. Victoire absolue ?
    if (g->score[player] >= WIN_SCORE || g->captures[player] >= 5) {
        return WIN_SCORE;
    }

    // 3. Score Relatif
    int score = g->score[player] - g->score[opponent];

    // Bonus/Malus Captures
    int my_caps = g->captures[player];
    score += (my_caps * 20000); 

    int opp_caps = g->captures[opponent];
    int capture_penalty = 0;
    switch (opp_caps) {
        case 0: capture_penalty = 0; break;
        case 1: capture_penalty = 20000; break;
        case 2: capture_penalty = 50000; break;
        case 3: capture_penalty = 200000; break;
        case 4: capture_penalty = 15000000; break;
    }
    score -= capture_penalty;

    // --- PEUR DE LA MORT ---
    if (g->score[opponent] >= CLOSED_FOUR) {
        score -= 30000000; 
    }

    return score;
}

/* 
 * Vérifie si un coup en (x,y) crée un "Free Three" strict dans la direction (dx, dy).
 * Un Free Three est un alignement de 3 pierres qui, si non bloqué, devient un Open Four.
 * Patterns reconnus :
 *  1.  . X X X .  (Standard)
 *  2.  . X _ X X . (Gap)
 *  3.  . X X _ X . (Gap)
 */
int check_free_three_pattern(game *g, int x, int y, int dx, int dy, int player) {
    // On simule la pose de la pierre
    g->board[y * BOARD_SIZE + x] = player;

    // On scanne une fenêtre de 7 cases autour du point (x,y) : [-3, +3]
    // On cherche les motifs .XXX. ou .X.XX. ou .XX.X.
    
    // Construction d'un mini-buffer de la ligne pour simplifier l'analyse
    int line[9]; // Indices 0 à 8. Le coup joué est au centre (index 4)
    int center = 4;
    
    for (int k = -4; k <= 4; k++) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        if (!IS_VALID(nx, ny)) line[center + k] = -1; // Bord
        else line[center + k] = g->board[ny * BOARD_SIZE + nx];
    }

    // On remet la case vide pour ne pas corrompre le plateau
    g->board[y * BOARD_SIZE + x] = EMPTY;

    // Analyse des motifs
    // Un Free Three doit avoir exactement 3 pierres et être ouvert des deux côtés.
    
    // Pattern 1 : . X X X . (Continu)
    // Le coup joué (X) peut être n'importe laquelle des 3 pierres.
    // On vérifie les fenêtres de 5 cases : [EMPTY, P, P, P, EMPTY]
    for (int k = -2; k <= 0; k++) { // Décalages possibles
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == player && 
            line[center+k+3] == player && 
            line[center+k+4] == EMPTY) return 1;
    }

    // Pattern 2 : . X _ X X . (Gap)
    // Fenêtre de 6 cases : [EMPTY, P, EMPTY, P, P, EMPTY]
    for (int k = -3; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == EMPTY && 
            line[center+k+3] == player && 
            line[center+k+4] == player && 
            line[center+k+5] == EMPTY) return 1;
    }

    // Pattern 3 : . X X _ X . (Gap inverse)
    // Fenêtre de 6 cases : [EMPTY, P, P, EMPTY, P, EMPTY]
    for (int k = -3; k <= 0; k++) {
        if (line[center+k] == EMPTY && 
            line[center+k+1] == player && 
            line[center+k+2] == player && 
            line[center+k+3] == EMPTY && 
            line[center+k+4] == player && 
            line[center+k+5] == EMPTY) return 1;
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
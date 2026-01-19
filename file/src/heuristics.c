#include "../include/gomoku.h"

/*
 * NOUVELLE FONCTION : Détecte les patterns gappés et retourne la case du trou
 * Retourne l'index de la case critique à bloquer, ou -1 si pas de gap critique
 */
static int find_gap_in_line(game *g, int x, int y, int dx, int dy, int player) {
    // Construire un buffer de 9 cases centré sur (x,y)
    int line[9];
    int indices[9];
    
    for (int k = -4; k <= 4; k++) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        int buf_idx = k + 4;
        
        if (IS_VALID(nx, ny)) {
            indices[buf_idx] = GET_INDEX(nx, ny);
            line[buf_idx] = g->board[indices[buf_idx]];
        } else {
            indices[buf_idx] = -1;
            line[buf_idx] = -1; // Hors plateau = bloqué
        }
    }
    
    // Chercher les patterns gappés de 4 pierres
    // Pattern: X_XXX (trou en position 1)
    for (int start = 0; start <= 4; start++) {
        if (line[start] == player &&
            line[start + 1] == EMPTY &&
            line[start + 2] == player &&
            line[start + 3] == player &&
            line[start + 4] == player) {
            return indices[start + 1]; // Le trou !
        }
    }
    
    // Pattern: XX_XX (trou au milieu)
    for (int start = 0; start <= 4; start++) {
        if (line[start] == player &&
            line[start + 1] == player &&
            line[start + 2] == EMPTY &&
            line[start + 3] == player &&
            line[start + 4] == player) {
            return indices[start + 2]; // Le trou !
        }
    }
    
    // Pattern: XXX_X (trou en position 3)
    for (int start = 0; start <= 4; start++) {
        if (line[start] == player &&
            line[start + 1] == player &&
            line[start + 2] == player &&
            line[start + 3] == EMPTY &&
            line[start + 4] == player) {
            return indices[start + 3]; // Le trou !
        }
    }
    
    return -1;
}

/*
 * Fonction exportée : Trouve la case critique d'un gapped four pour un joueur
 */
int find_gapped_four_hole(game *g, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int hole = find_gap_in_line(g, x, y, dx[d], dy[d], player);
            if (hole != -1) {
                return hole;
            }
        }
    }
    return -1;
}

/*
 * NOUVELLE FONCTION : Détecte les Gapped THREE (3 pierres + 1 trou + espaces libres aux bords)
 * Patterns dangereux : .X_XX. ou .XX_X.
 * Retourne l'index du trou, ou -1 si pas trouvé
 */
static int find_gapped_three_in_line(game *g, int x, int y, int dx, int dy, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // Construire un buffer de 7 cases centré sur (x,y)
    int line[7];
    int indices[7];
    
    for (int k = -3; k <= 3; k++) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        int buf_idx = k + 3;
        
        if (IS_VALID(nx, ny)) {
            indices[buf_idx] = GET_INDEX(nx, ny);
            line[buf_idx] = g->board[indices[buf_idx]];
        } else {
            indices[buf_idx] = -1;
            line[buf_idx] = opponent; // Hors plateau = bloqué
        }
    }
    
    // Pattern: . X _ X X . (trou en position 2, fenêtre de 6)
    // Positions: 0=empty, 1=player, 2=empty(hole), 3=player, 4=player, 5=empty
    for (int start = 0; start <= 1; start++) {
        if (start + 5 >= 7) continue;
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == EMPTY &&
            line[start + 3] == player &&
            line[start + 4] == player &&
            line[start + 5] == EMPTY) {
            return indices[start + 2]; // Le trou !
        }
    }
    
    // Pattern: . X X _ X . (trou en position 3, fenêtre de 6)
    for (int start = 0; start <= 1; start++) {
        if (start + 5 >= 7) continue;
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == player &&
            line[start + 3] == EMPTY &&
            line[start + 4] == player &&
            line[start + 5] == EMPTY) {
            return indices[start + 3]; // Le trou !
        }
    }
    
    return -1;
}

/*
 * Fonction exportée : Trouve la case critique d'un gapped THREE pour un joueur
 * C'est CRITIQUE car un gapped three non bloqué devient un gapped four !
 */
int find_gapped_three_hole(game *g, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int hole = find_gapped_three_in_line(g, x, y, dx[d], dy[d], player);
            if (hole != -1) {
                return hole;
            }
        }
    }
    return -1;
}

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
        if (open_ends == 2) score = OPEN_THREE;
        else if (open_ends == 1) score = CLOSED_THREE;
    }
    else if (len == 2) {
        if (open_ends == 2) score = OPEN_TWO; 
        else if (open_ends == 1) score = CLOSED_TWO;
    }

    // --- 4. DÉTECTION AVANCÉE DES TROUS (GAPPED PATTERNS) ---
    // CORRECTION MAJEURE : Détecter X_XXX, XX_XX, XXX_X comme WIN potentiel
    
    if (len < 5 && (start_open || end_open)) {
        // --- Gap Positif (après la séquence) ---
        if (end_open && i < 5) {
            int gap_x = x + dx * i; // Position du premier EMPTY trouvé
            int gap_y = y + dy * i;
            
            // Compter les pierres APRÈS le trou
            int extra_len = 0;
            for (int k = 1; k <= 4; k++) {
                int ppx = gap_x + dx * k;
                int ppy = gap_y + dy * k;
                if (!IS_VALID(ppx, ppy)) break;
                if (g->board[GET_INDEX(ppx, ppy)] == player) extra_len++;
                else break;
            }
            
            int virtual_len = len + extra_len;
            
            // GAPPED FOUR = Victoire imminente (un seul coup pour gagner)
            if (virtual_len >= 4) {
                // C'est un Gapped Four ! Score très élevé
                if (score < CLOSED_FOUR) score = CLOSED_FOUR;
                // Si c'est exactement 4 avec le trou = WIN en un coup
                if (virtual_len == 4) score = OPEN_FOUR; // Traiter comme Open Four (menace de victoire)
            }
            else if (virtual_len == 3) {
                if (start_open) {
                    if (score < OPEN_THREE) score = OPEN_THREE;
                } else {
                    if (score < CLOSED_THREE) score = CLOSED_THREE;
                }
            }
        }

        // --- Gap Négatif (avant la séquence) ---
        if (start_open && j < 5) {
            int gap_x = x - dx * j;
            int gap_y = y - dy * j;
            
            int extra_len = 0;
            for (int k = 1; k <= 4; k++) {
                int mmx = gap_x - dx * k;
                int mmy = gap_y - dy * k;
                if (!IS_VALID(mmx, mmy)) break;
                if (g->board[GET_INDEX(mmx, mmy)] == player) extra_len++;
                else break;
            }
            
            int virtual_len = len + extra_len;
            
            if (virtual_len >= 4) {
                if (score < CLOSED_FOUR) score = CLOSED_FOUR;
                if (virtual_len == 4) score = OPEN_FOUR;
            }
            else if (virtual_len == 3) {
                if (end_open) {
                    if (score < OPEN_THREE) score = OPEN_THREE;
                } else {
                    if (score < CLOSED_THREE) score = CLOSED_THREE;
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
    if (opp_max_threat >= OPEN_THREE) {
        if (my_max_threat < CLOSED_FOUR) {
            final_score -= (long long)(opp_max_threat * 3);
        } else {
            final_score -= (long long)(opp_max_threat);
        }
    } else if (opp_max_threat >= CLOSED_THREE) {
        final_score -= (long long)(opp_max_threat * 2);
    }

    // 4. Captures (valeur augmentée)
    final_score += (g->captures[player] * CAPTURE_BONUS); 
    final_score -= (g->captures[opponent] * CAPTURE_BONUS);
    
    // 5. (NOUVEAU) Pénalité pour paires vulnérables
    int my_vulnerable = count_vulnerable_pairs(g, player);
    int opp_vulnerable = count_vulnerable_pairs(g, opponent);
    
    // Chaque paire vulnérable est un risque, surtout si l'adversaire capture déjà
    int vulnerability_penalty = my_vulnerable * 25000;
    if (g->captures[opponent] >= 2) {
        vulnerability_penalty *= 2; // Double pénalité si l'adversaire capture déjà
    }
    if (g->captures[opponent] >= 3) {
        vulnerability_penalty *= 2; // Quadruple pénalité si proche de la victoire
    }
    final_score -= vulnerability_penalty;
    
    // Bonus pour paires adverses vulnérables (opportunité de capture)
    int vulnerability_bonus = opp_vulnerable * 20000;
    if (g->captures[player] >= 2) {
        vulnerability_bonus *= 2;
    }
    final_score += vulnerability_bonus;
    
    // 6. (NOUVEAU) Bonus/Malus basé sur l'écart de captures
    int capture_diff = g->captures[player] - g->captures[opponent];
    if (capture_diff > 0) {
        // On a l'avantage en captures
        final_score += capture_diff * 30000;
    } else if (capture_diff < 0) {
        // L'adversaire a l'avantage en captures
        final_score += capture_diff * 40000; // Pénalité plus forte
    }

    // NOUVEAU : Intégrer les captures dans le score global
    int my_capture_threat = 0;
    int opp_capture_threat = 0;

    // Calculer les menaces de capture
    if (g->captures[player] >= 4) my_capture_threat = WIN_SCORE;
    else if (g->captures[player] >= 3) my_capture_threat = CLOSED_FOUR;
    else if (g->captures[player] >= 2) my_capture_threat = OPEN_THREE;

    if (g->captures[opponent] >= 4) opp_capture_threat = WIN_SCORE;
    else if (g->captures[opponent] >= 3) opp_capture_threat = CLOSED_FOUR;
    else if (g->captures[opponent] >= 2) opp_capture_threat = OPEN_THREE;

    // Ajouter au score final
    final_score += my_capture_threat / 10;  // Bonus pour nos captures
    final_score -= opp_capture_threat / 5;  // Pénalité pour captures adverses (plus forte)

    // 7. Clamp
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
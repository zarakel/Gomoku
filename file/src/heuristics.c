#include "../include/gomoku.h"

/*
 * Détecte les patterns gappés et retourne la case du trou
 * Scanne TOUTES les fenêtres de 5 cases possibles, pas seulement celles centrées sur une pierre
 */
static int find_gap_in_line(game *g, int x, int y, int dx, int dy, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
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
            line[buf_idx] = opponent; // Hors plateau = bloqué
        }
    }
    
    // Chercher les patterns gappés de 4 pierres dans TOUTES les fenêtres de 5
    for (int start = 0; start <= 4; start++) {
        // Vérifier que les indices sont valides
        if (indices[start] == -1 || indices[start + 4] == -1) continue;
        
        // Compter les pierres et trouver le trou
        int stones = 0;
        int hole_pos = -1;
        int hole_count = 0;
        bool blocked = false;
        
        for (int i = 0; i < 5; i++) {
            if (line[start + i] == player) {
                stones++;
            } else if (line[start + i] == EMPTY) {
                hole_count++;
                hole_pos = start + i;
            } else {
                blocked = true;
                break;
            }
        }
        
        // Pattern valide : 4 pierres + 1 trou = Gapped Four !
        if (!blocked && stones == 4 && hole_count == 1 && hole_pos != -1) {
            if (indices[hole_pos] != -1) {
                return indices[hole_pos];
            }
        }
    }
    
    return -1;
}

int get_threat_level(int score) {
    if (score >= WIN_SCORE) return IDX_WIN;
    if (score >= OPEN_FOUR) return IDX_OPEN_FOUR; // Inclut les Gapped Fours gagnants
    if (score >= CLOSED_FOUR) return IDX_CLOSED_FOUR; // Inclut les menaces fortes
    if (score >= OPEN_THREE) return IDX_OPEN_THREE;
    if (score >= CLOSED_THREE) return IDX_CLOSED_THREE;
    return IDX_OTHERS;
}

void refresh_board_stats(game *g) {
    // 1. Reset complet des structures
    g->pos_score[1] = 0;
    g->pos_score[2] = 0;
    memset(g->threat_counts, 0, sizeof(g->threat_counts));
    memset(g->max_threat_level, 0, sizeof(g->max_threat_level));

    // 2. Scan complet du plateau
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] == EMPTY) continue;
        
        // On simule l'ajout de la pierre pour déclencher les mises à jour
        // Note: update_impacted_scores ajoute le score, donc c'est parfait.
        update_impacted_scores(g, GET_X(i), GET_Y(i), false); // false = ADD
    }
}

/*
 * Fonction exportée : Trouve la case critique d'un gapped four pour un joueur
 */
/*
 * Fonction exportée : Trouve la case critique d'un gapped four pour un joueur
 * CORRIGÉ : Scanne depuis chaque pierre ET vérifie dans les deux directions
 */
int find_gapped_four_hole(game *g, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    // Méthode 1 : Scanner depuis chaque pierre du joueur
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
    
    // Méthode 2 : Scanner depuis chaque case VIDE (pour détecter si c'est un trou)
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            // Compter les pierres de chaque côté du trou
            int stones_pos = 0;
            int stones_neg = 0;
            
            // Scan positif
            for (int k = 1; k <= 4; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                if (g->board[GET_INDEX(nx, ny)] == player) stones_pos++;
                else break;
            }
            
            // Scan négatif
            for (int k = 1; k <= 4; k++) {
                int nx = x - dx[d] * k;
                int ny = y - dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                if (g->board[GET_INDEX(nx, ny)] == player) stones_neg++;
                else break;
            }
            
            // Si total = 4 pierres autour du trou → Gapped Four !
            if (stones_pos + stones_neg == 4) {
                return idx;
            }
            
            // Patterns asymétriques : 1+3, 2+2, 3+1
            if ((stones_pos >= 1 && stones_neg >= 3) ||
                (stones_pos >= 2 && stones_neg >= 2) ||
                (stones_pos >= 3 && stones_neg >= 1)) {
                if (stones_pos + stones_neg >= 4) {
                    return idx;
                }
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
/*
 * Fonction exportée : Trouve la case critique d'un gapped THREE pour un joueur
 * CORRIGÉ : Vérifie aussi depuis les cases vides
 */
int find_gapped_three_hole(game *g, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int opponent = (player == P1) ? P2 : P1;
    
    // Méthode 1 : Scanner depuis chaque pierre du joueur
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
    
    // Méthode 2 : Scanner depuis chaque case VIDE
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int stones_pos = 0;
            int stones_neg = 0;
            bool open_pos = false;
            bool open_neg = false;
            
            // Scan positif
            for (int k = 1; k <= 3; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones_pos++;
                else if (cell == EMPTY) { open_pos = true; break; }
                else break;
            }
            
            // Scan négatif
            for (int k = 1; k <= 3; k++) {
                int nx = x - dx[d] * k;
                int ny = y - dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones_neg++;
                else if (cell == EMPTY) { open_neg = true; break; }
                else break;
            }
            
            // Gapped Open Three : 3 pierres + 2 bouts ouverts
            if (stones_pos + stones_neg == 3 && open_pos && open_neg) {
                return idx;
            }
        }
    }
    
    return -1;
}

// Helper inline pour éviter la répétition de code
int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
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

/* Met à jour les statistiques (Score total + Compteurs de menaces) */
static void update_stats(game *g, int player, int score, bool remove_mode) {
    if (score == 0) return;

    int lvl = get_threat_level(score);

    if (remove_mode) {
        g->pos_score[player] -= score;
        g->threat_counts[player][lvl]--;
    } else {
        g->pos_score[player] += score;
        g->threat_counts[player][lvl]++;
    }

    // Mise à jour paresseuse du Max Threat
    // On part du haut (WIN) et on descend jusqu'à trouver un compteur > 0
    for (int i = IDX_WIN; i >= 0; i--) {
        if (g->threat_counts[player][i] > 0) {
            g->max_threat_level[player] = i;
            return;
        }
    }
    g->max_threat_level[player] = 0;
}

/*
 * Cœur de l'incrémental : Scanne une petite fenêtre autour de (x,y)
 * pour trouver les "Têtes de lignes" impactées et mettre à jour leur score.
 */
void update_impacted_scores(game *g, int x, int y, bool remove_mode) {
    // Directions : Horizontal, Vertical, Diag1, Diag2
    int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];

        // On regarde une fenêtre de [-5, +0] autour de x,y pour trouver les DÉBUTS de lignes
        // qui pourraient passer par (x,y).
        for (int k = -5; k <= 0; k++) {
            int sx = x + k * dx;
            int sy = y + k * dy;

            if (!IS_VALID(sx, sy)) continue;

            int p = g->board[GET_INDEX(sx, sy)];
            if (p == EMPTY) continue;

            // Vérifie si c'est bien le début d'une séquence pour ce joueur
            // (C'est-à-dire que la case d'avant est hors-plateau ou d'une autre couleur)
            int prev_x = sx - dx;
            int prev_y = sy - dy;
            bool is_start = false;

            if (!IS_VALID(prev_x, prev_y)) {
                is_start = true;
            } else if (g->board[GET_INDEX(prev_x, prev_y)] != p) {
                is_start = true;
            }

            if (is_start) {
                // On évalue cette ligne qui passe potentiellement par (x,y)
                // Note : On re-vérifie si elle passe vraiment par la zone d'impact si on veut optimiser,
                // mais ici on réévalue simplement les têtes proches, ce qui est sûr et rapide.
                int score = evaluate_line(g, sx, sy, dx, dy, p);
                update_stats(g, p, score, remove_mode);
            }
        }
    }
}

/*
 * Nouvelle version O(1) de evaluate_board
 * Elle utilise les valeurs pré-calculées.
 */
int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;

    // 1. Victoire immédiate détectée par les compteurs
    if (g->max_threat_level[player] == IDX_WIN) return WIN_SCORE;
    if (g->max_threat_level[opponent] == IDX_WIN) return -WIN_SCORE;
    
    // Victoire par capture
    if (g->captures[opponent] >= 5) return -WIN_SCORE; 
    if (g->captures[player] >= 5) return WIN_SCORE;

    // 2. Score Positionnel de Base (O(1))
    long long final_score = g->pos_score[player] - g->pos_score[opponent];

    // 3. Mode Panique basé sur les BUCKETS (Plus fiable que l'ancien max_threat)
    int opp_threat_lvl = g->max_threat_level[opponent];
    int my_threat_lvl = g->max_threat_level[player];

    if (opp_threat_lvl >= IDX_OPEN_THREE) {
        // Calcul du facteur de panique
        long long panic_val = 0;
        if (opp_threat_lvl == IDX_OPEN_FOUR) panic_val = OPEN_FOUR;
        else if (opp_threat_lvl == IDX_CLOSED_FOUR) panic_val = CLOSED_FOUR;
        else if (opp_threat_lvl == IDX_OPEN_THREE) panic_val = OPEN_THREE;

        // Si on n'a pas de contre-menace plus forte, on panique
        if (my_threat_lvl < IDX_CLOSED_FOUR) {
            final_score -= (panic_val * 4);
        } else {
            final_score -= panic_val;
        }
    } else if (opp_threat_lvl == IDX_CLOSED_THREE) {
        final_score -= CLOSED_THREE * 2;
    }

    int opp_caps = g->captures[opponent];
    
    if (opp_caps >= 4) {
        /* 4 paires = ALERTE ROUGE (Une erreur et c'est perdu) */
        // On met une valeur proche de la défaite pour forcer l'IA à tout faire pour éviter ça
        final_score -= 1500000000; // -1.5 Milliards (Proche de -2Mrd WIN)
    }
    else if (opp_caps >= 3) {
        /* 3 paires = TRES GRAVE */
        final_score -= 300000000; // -300 Millions (Moitié d'un Open 3)
    }
    else if (opp_caps >= 2) {
        /* 2 paires = Dangereux */
        final_score -= 50000000; // Augmenté x10
    }

    // 4. Bonus de Captures
    final_score += (g->captures[player] * CAPTURE_BONUS); 
    final_score -= (g->captures[opponent] * CAPTURE_BONUS);

    if (final_score < WIN_SCORE && final_score > -WIN_SCORE) {
        int opp_threat_lvl = (player == P1) ? g->max_threat_level[P2] : g->max_threat_level[P1]; // Si vous utilisez les buckets
        // OU si vous utilisez l'ancienne version :
        // int opp_threat_lvl = (player == P1) ? max_p2_threat : max_p1_threat;
        
        // Seuil de déclenchement : On ne lance le VCF (coûteux) que s'il y a danger
        // OPEN_THREE est le minimum pour commencer une attaque forcée.
        if (opp_threat_lvl >= OPEN_THREE) {
            // On limite la profondeur à 6 ou 8 coups pour rester rapide
            // On passe 'g->ia_timer.start_ts' converti ou on utilise clock()
            if (check_vcf_win(g, opponent, 0, 8, clock())) {
                return -WIN_SCORE; // ABANDONNER CETTE BRANCHE, C'EST LA MORT
            }
        }
    }
    
    // Clamp pour rester dans les bornes int
    if (final_score > WIN_SCORE) return WIN_SCORE;
    if (final_score < -WIN_SCORE) return -WIN_SCORE;

    /* DÉTECTION DES FOURCHETTES (FORKS) ADVERSES */
    if (final_score > -WIN_SCORE && final_score < WIN_SCORE) {
        int fork_penalty = 0;
        
        // On scanne les cases vides intéressantes
        // (Optimisation : limiter aux alentours du dernier coup ou des menaces)
        for (int i = 0; i < MAX_BOARD; i++) {
            if (g->board[i] != EMPTY) continue;
            
            // Si l'adversaire joue ici, est-ce qu'il nous tue ?
            int val = compute_fork_value(g, i, opponent);
            if (val > 0) {
                // Il a une fourchette !
                fork_penalty = 1500000000; // -1.5 Milliards
                break; // Une seule suffit pour perdre
            }
        }
        final_score -= fork_penalty;
    }

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
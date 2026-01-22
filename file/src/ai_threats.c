#include "../include/gomoku.h"

/*
 * ai_threats.c - Détection et analyse des menaces
 * 
 * Responsabilité unique : Détecter les menaces sur le plateau
 * - Menaces existantes (alignements déjà formés)
 * - Menaces potentielles (si un joueur joue à une case)
 * - Patterns gappés (X_XXX, .X_XX.)
 */


/* Dans ai_threats.c ou heuristics.c */

static int count_empty_neighbors(game *g, int idx) {
    if (idx == -1) return 0;
    
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int count = 0;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == EMPTY) {
                count++;
            }
        }
    }
    
    return count;
}

/* ============================================================================
 * SCAN DES MENACES EXISTANTES
 * Analyse les alignements déjà présents sur le plateau
 * ============================================================================ */

int scan_all_existing_threats(game *g, int player, ExistingThreat *threats, int max_threats) {
    int threat_count = 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int opponent = (player == P1) ? P2 : P1;
    
    for (int idx = 0; idx < MAX_BOARD && threat_count < max_threats; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            /* Ne scanner que si c'est le DÉBUT de la ligne */
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) 
                continue;
            
            /* ══════════════════════════════════════════════════════════════
             * CORRECTION : Comptage simple et direct des pierres
             * ══════════════════════════════════════════════════════════════ */
            
            int stones = 1;  /* On compte la pierre de départ */
            int empty_before = -1;
            int empty_after = -1;
            int last_stone_x = x;
            int last_stone_y = y;
            
            /* Vérifier case AVANT le début (pour savoir si ouvert) */
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == EMPTY) {
                empty_before = GET_INDEX(px, py);
            }
            
            /* Compter les pierres dans la direction positive */
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                
                if (!IS_VALID(nx, ny)) break;
                
                int cell = g->board[GET_INDEX(nx, ny)];
                
                if (cell == player) {
                    stones++;
                    last_stone_x = nx;
                    last_stone_y = ny;
                }
                else if (cell == EMPTY) {
                    empty_after = GET_INDEX(nx, ny);
                    break;
                }
                else {
                    /* Bloqué par l'adversaire */
                    break;
                }
            }
            
            int open_ends = (empty_before != -1 ? 1 : 0) + (empty_after != -1 ? 1 : 0);
            
            /* ══════════════════════════════════════════════════════════════
             * ÉVALUATION DE LA MENACE
             * ══════════════════════════════════════════════════════════════ */
            
            int threat_score = 0;
            int best_block = -1;
            
            /* 5+ pierres = VICTOIRE (ne devrait pas arriver mais au cas où) */
            if (stones >= 5) {
                threat_score = WIN_SCORE;
                best_block = -1;  /* Pas de blocage possible */
            }
            /* 4 pierres = CRITIQUE */
            else if (stones == 4) {
                if (open_ends >= 2) {
                    threat_score = OPEN_FOUR;  /* Imparable ! */
                    best_block = (empty_after != -1) ? empty_after : empty_before;
                } else if (open_ends == 1) {
                    threat_score = CLOSED_FOUR;
                    best_block = (empty_after != -1) ? empty_after : empty_before;
                } else {
                    threat_score = CLOSED_FOUR / 2;  /* Bloqué des deux côtés */
                    best_block = -1;
                }
            }
            /* 3 pierres = SÉRIEUX */
            else if (stones == 3) {
                if (open_ends == 2) {
                    threat_score = OPEN_THREE;
                    /* Pour un OPEN_THREE, bloquer du côté le plus dangereux */
                    best_block = (empty_after != -1) ? empty_after : empty_before;
                } else if (open_ends == 1) {
                    threat_score = CLOSED_THREE;
                    best_block = (empty_after != -1) ? empty_after : empty_before;
                }
            }
            /* 2 pierres avec 2 bouts ouverts = potentiel (mais pas urgent) */
            else if (stones == 2 && open_ends == 2) {
                threat_score = OPEN_TWO;
                best_block = -1;  /* Pas urgent de bloquer */
            }
            
            /* ══════════════════════════════════════════════════════════════
             * STOCKER LA MENACE SI SIGNIFICATIVE
             * Seuil abaissé à CLOSED_THREE pour ne rien rater
             * ══════════════════════════════════════════════════════════════ */
            
            if (threat_score >= CLOSED_THREE && best_block != -1 && threat_count < max_threats) {
                /* Vérifier qu'on n'a pas déjà cette menace */
                bool already_exists = false;
                for (int t = 0; t < threat_count; t++) {
                    if (threats[t].block_idx == best_block && threats[t].direction == d) {
                        already_exists = true;
                        if (threat_score > threats[t].score) {
                            threats[t].score = threat_score;
                            threats[t].stones = stones;
                        }
                        break;
                    }
                }
                
                if (!already_exists) {
                    threats[threat_count].block_idx = best_block;
                    threats[threat_count].score = threat_score;
                    threats[threat_count].direction = d;
                    threats[threat_count].stones = stones;
                    threat_count++;
                    
                    #ifdef DEBUG
                    char* dir_names[] = {"H", "V", "D\\", "D/"};
                    printf("  THREAT FOUND: %d stones in %s from (%d,%d), score=%d, block=(%d,%d)\n",
                           stones, dir_names[d], x, y, threat_score,
                           GET_X(best_block), GET_Y(best_block));
                    #endif
                }
            }
        }
    }
    
    /* ══════════════════════════════════════════════════════════════
     * TRIER PAR PRIORITÉ : PIERRES > SCORE
     * 4 pierres est TOUJOURS plus urgent que 3 pierres
     * ══════════════════════════════════════════════════════════════ */
    
    for (int i = 0; i < threat_count - 1; i++) {
        for (int j = i + 1; j < threat_count; j++) {
            bool should_swap = false;
            
            /* PRIORITÉ 1 : Plus de pierres = plus urgent */
            if (threats[j].stones > threats[i].stones) {
                should_swap = true;
            }
            /* PRIORITÉ 2 : À pierres égales, score plus élevé */
            else if (threats[j].stones == threats[i].stones && 
                     threats[j].score > threats[i].score) {
                should_swap = true;
            }
            
            if (should_swap) {
                ExistingThreat tmp = threats[i];
                threats[i] = threats[j];
                threats[j] = tmp;
            }
        }
    }
    
    /* NOUVEAU TRI : À pierres et score égaux, prioriser par potentiel de danger */
    for (int i = 0; i < threat_count - 1; i++) {
        for (int j = i + 1; j < threat_count; j++) {
            bool should_swap = false;
            
            /* PRIORITÉ 1 : Plus de pierres = plus urgent */
            if (threats[j].stones > threats[i].stones) {
                should_swap = true;
            }
            /* PRIORITÉ 2 : À pierres égales, score plus élevé */
            else if (threats[j].stones == threats[i].stones) {
                if (threats[j].score > threats[i].score) {
                    should_swap = true;
                }
                /* PRIORITÉ 3 : À pierres et score égaux, évaluer le potentiel */
                else if (threats[j].score == threats[i].score) {
                    /* Calculer combien de cases libres autour de chaque blocage */
                    int pot_i = count_empty_neighbors(g, threats[i].block_idx);
                    int pot_j = count_empty_neighbors(g, threats[j].block_idx);
                    
                    /* Plus de voisins vides = plus de potentiel d'extension */
                    if (pot_j > pot_i) {
                        should_swap = true;
                    }
                }
            }
            
            if (should_swap) {
                ExistingThreat tmp = threats[i];
                threats[i] = threats[j];
                threats[j] = tmp;
            }
        }
    }
    
    return threat_count;
}

/* ============================================================================
 * COMPTAGE DES MENACES SÉRIEUSES
 * Compte les lignes de 3+ pierres avec au moins un bout ouvert
 * ============================================================================ */

int count_serious_threats(game *g, int player) {
    int threat_count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    bool counted[MAX_BOARD] = {false};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            /* Ne scanner que le début de ligne */
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) 
                continue;
            
            int stones = 1;
            int open_ends = 0;
            
            /* Scan positif */
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    open_ends++;
                    break;
                }
                else break;
            }
            
            /* Scan négatif */
            int bx = x - dx[d];
            int by = y - dy[d];
            if (IS_VALID(bx, by) && g->board[GET_INDEX(bx, by)] == EMPTY) {
                open_ends++;
            }
            
            /* Menace sérieuse : 3+ pierres avec espace */
            if (stones >= 3 && open_ends >= 1 && !counted[idx]) {
                threat_count++;
                counted[idx] = true;
            }
        }
    }
    return threat_count;
}

/* ============================================================================
 * COMPTAGE DES GAPPED THREES
 * Patterns : .X_XX. ou .XX_X.
 * ============================================================================ */

int count_gapped_threes(game *g, int player) {
    int count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int opponent = (player == P1) ? P2 : P1;
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int line[7];
            for (int k = -3; k <= 3; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (IS_VALID(nx, ny)) {
                    line[k + 3] = g->board[GET_INDEX(nx, ny)];
                } else {
                    line[k + 3] = opponent; /* Hors plateau = bloqué */
                }
            }
            
            /* Pattern: . X _ X X . */
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == EMPTY &&
                    line[start + 3] == player &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
            
            /* Pattern: . X X _ X . */
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == player &&
                    line[start + 3] == EMPTY &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
        }
    }
    return count;
}

/* ============================================================================
 * ÉVALUATION D'UN COUP AVEC CAPTURES
 * Simule le coup, applique les captures, retourne le meilleur score
 * ============================================================================ */

int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    /* NOUVEAU : Si capture, scanner TOUTES les lignes affectées */
    if (undo.captured_count > 0) {
        
        /* 1. Scanner les lignes passant par les pierres capturées */
        for (int i = 0; i < undo.captured_count; i++) {
            int cap_idx = undo.captured_indices[i];
            int cx = GET_X(cap_idx);
            int cy = GET_Y(cap_idx);
            
            /* Scanner dans les 4 directions depuis la case vidée */
            int dx[] = {1, 0, 1, 1};
            int dy[] = {0, 1, 1, -1};
            
            for (int d = 0; d < 4; d++) {
                /* Chercher nos pierres de chaque côté de la case vidée */
                int stones_pos = 0, stones_neg = 0;
                int first_stone_pos = -1, first_stone_neg = -1;
                
                /* Scan positif */
                for (int k = 1; k <= 5; k++) {
                    int nx = cx + dx[d] * k;
                    int ny = cy + dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    int cell = g->board[GET_INDEX(nx, ny)];
                    if (cell == player) {
                        stones_pos++;
                        if (first_stone_pos == -1) first_stone_pos = GET_INDEX(nx, ny);
                    }
                    else if (cell != EMPTY) break;
                }
                
                /* Scan négatif */
                for (int k = 1; k <= 5; k++) {
                    int nx = cx - dx[d] * k;
                    int ny = cy - dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    int cell = g->board[GET_INDEX(nx, ny)];
                    if (cell == player) {
                        stones_neg++;
                        if (first_stone_neg == -1) first_stone_neg = GET_INDEX(nx, ny);
                    }
                    else if (cell != EMPTY) break;
                }
                
                /* La capture a-t-elle CONNECTÉ deux segments ? */
                if (stones_pos >= 1 && stones_neg >= 1) {
                    int total_connected = stones_pos + stones_neg;
                    
                    /* Évaluer la nouvelle ligne connectée */
                    if (first_stone_pos != -1) {
                        int new_score = get_point_score(g, GET_X(first_stone_pos), 
                                                        GET_Y(first_stone_pos), player);
                        if (new_score > score) score = new_score;
                    }
                    
                    /* Bonus pour connexion de segments */
                    if (total_connected >= 3) score += OPEN_THREE / 2;
                    if (total_connected >= 4) score += CLOSED_FOUR;
                }
            }
        }
        
        /* 2. Re-scanner le coup lui-même (les captures peuvent avoir ouvert des lignes) */
        int post_score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
        if (post_score > score) score = post_score;
    }
    
    undo_move(g, player, &undo);
    return score;
}

/* ============================================================================
 * RECHERCHE DE TOUTES LES MENACES (existantes + futures)
 * Retourne l'index et le score de la meilleure menace trouvée
 * ============================================================================ */

void find_all_threats(game *g, int player, int *best_idx, int *best_score) {
    *best_idx = -1;
    *best_score = 0;
    
    /* 1. Scanner les menaces EXISTANTES avec la nouvelle fonction */
    ExistingThreat existing_threats[20];
    int existing_count = scan_all_existing_threats(g, player, existing_threats, 20);
    
    int existing_block = -1;
    int existing_threat = 0;
    
    if (existing_count > 0) {
        existing_block = existing_threats[0].block_idx;
        existing_threat = existing_threats[0].score;
    }
    
    #ifdef DEBUG
    printf("DEBUG find_all_threats: existing=%d block=(%d,%d)\n",
           existing_threat,
           existing_block != -1 ? GET_X(existing_block) : -1,
           existing_block != -1 ? GET_Y(existing_block) : -1);
    #endif
    
    if (existing_threat > *best_score) {
        *best_score = existing_threat;
        *best_idx = existing_block;
    }
    
    /* 2. Scanner les menaces FUTURES (si on joue à chaque case) */
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* CORRECTION : Vérifier que la case a des voisins */
        bool has_neighbor = false;
        int x = GET_X(i), y = GET_Y(i);
        for (int dy = -2; dy <= 2 && !has_neighbor; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (IS_VALID(x+dx, y+dy) && g->board[GET_INDEX(x+dx, y+dy)] != EMPTY) {
                    has_neighbor = true;
                    break;
                }
            }
        }
        if (!has_neighbor) continue;
        
        int score = evaluate_move_with_captures_full(g, i, player);
        
        if (score > *best_score) {
            *best_score = score;
            *best_idx = i;
        }
    }
}

/* ============================================================================
 * DÉTECTION DES MENACES CONVERGENTES
 * Trouve si le joueur a plusieurs menaces qui partagent une case de blocage
 * ou qui ne peuvent pas être toutes bloquées en un coup
 * 
 * Retourne le nombre de menaces sérieuses (>= OPEN_THREE)
 * Si >= 2, c'est potentiellement une double menace imparable
 * ============================================================================ */

int detect_convergent_threats(game *g, int player, int *critical_moves, int *critical_count) {
    int threats_found = 0;
    *critical_count = 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    /* Structure pour stocker les menaces */
    typedef struct {
        int start_idx;
        int direction;
        int score;
        int block_points[4];  /* Cases qui bloqueraient cette menace */
        int block_count;
    } ThreatInfo;
    
    ThreatInfo threats[20];
    int threat_count = 0;
    
    /* Scanner toutes les lignes du joueur */
    for (int idx = 0; idx < MAX_BOARD && threat_count < 20; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            /* Vérifier qu'on est au début de la ligne */
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player)
                continue;
            
            int stones = 1;
            int empty_before = -1;
            int empty_after = -1;
            int gaps[4] = {-1, -1, -1, -1};
            int gap_count = 0;
            
            /* Vérifier case avant */
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == EMPTY) {
                empty_before = GET_INDEX(px, py);
            }
            
            /* Scanner la ligne */
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) {
                    stones++;
                } else if (cell == EMPTY) {
                    if (stones >= 2 && gap_count < 4) {
                        gaps[gap_count++] = GET_INDEX(nx, ny);
                    }
                    if (empty_after == -1) {
                        empty_after = GET_INDEX(nx, ny);
                    }
                    break;
                } else {
                    break;
                }
            }
            
            /* Évaluer si c'est une menace sérieuse */
            int open_ends = (empty_before != -1 ? 1 : 0) + (empty_after != -1 ? 1 : 0);
            int threat_score = 0;
            
            if (stones >= 4 && open_ends >= 1) threat_score = CLOSED_FOUR;
            else if (stones == 3 && open_ends == 2) threat_score = OPEN_THREE;
            else if (stones == 3 && open_ends == 1) threat_score = CLOSED_THREE;
            
            /* Stocker la menace si elle est sérieuse */
            if (threat_score >= CLOSED_THREE && threat_count < 20) {
                threats[threat_count].start_idx = idx;
                threats[threat_count].direction = d;
                threats[threat_count].score = threat_score;
                threats[threat_count].block_count = 0;
                
                if (empty_before != -1) {
                    threats[threat_count].block_points[threats[threat_count].block_count++] = empty_before;
                }
                if (empty_after != -1) {
                    threats[threat_count].block_points[threats[threat_count].block_count++] = empty_after;
                }
                for (int g_idx = 0; g_idx < gap_count && threats[threat_count].block_count < 4; g_idx++) {
                    threats[threat_count].block_points[threats[threat_count].block_count++] = gaps[g_idx];
                }
                
                threat_count++;
                if (threat_score >= OPEN_THREE) threats_found++;
            }
        }
    }
    
    /* Analyser les convergences */
    /* Une convergence = deux menaces qui ne peuvent pas être bloquées par un seul coup */
    
    for (int i = 0; i < threat_count && *critical_count < 10; i++) {
        for (int j = i + 1; j < threat_count; j++) {
            /* Vérifier si les deux menaces partagent un point de blocage */
            bool shared_block = false;
            int shared_point = -1;
            
            for (int bi = 0; bi < threats[i].block_count && !shared_block; bi++) {
                for (int bj = 0; bj < threats[j].block_count; bj++) {
                    if (threats[i].block_points[bi] == threats[j].block_points[bj]) {
                        shared_block = true;
                        shared_point = threats[i].block_points[bi];
                        break;
                    }
                }
            }
            
            /* Si pas de point commun et les deux sont OPEN_THREE+ → double menace ! */
            if (!shared_block && 
                threats[i].score >= OPEN_THREE && 
                threats[j].score >= OPEN_THREE) {
                /* Ajouter tous les points de blocage comme critiques */
                for (int bi = 0; bi < threats[i].block_count && *critical_count < 10; bi++) {
                    bool already_added = false;
                    for (int k = 0; k < *critical_count; k++) {
                        if (critical_moves[k] == threats[i].block_points[bi]) {
                            already_added = true;
                            break;
                        }
                    }
                    if (!already_added) {
                        critical_moves[(*critical_count)++] = threats[i].block_points[bi];
                    }
                }
            }
            
            /* Si point commun, c'est LE point critique à occuper */
            if (shared_block && shared_point != -1 && *critical_count < 10) {
                bool already_added = false;
                for (int k = 0; k < *critical_count; k++) {
                    if (critical_moves[k] == shared_point) {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added) {
                    critical_moves[(*critical_count)++] = shared_point;
                }
            }
        }
    }
    
    return threats_found;
}
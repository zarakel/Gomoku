#include "../include/gomoku.h"

/**
 * Detecte les patterns gappes (4 pierres + 1 trou) dans une ligne.
 * 
 * Patterns recherches :
 * - X_XXX : 4 pierres avec 1 trou au milieu
 * - XX_XX, XXX_X, etc. : toutes les positions du trou
 * 
 * Retourne l'index du trou, ou -1 si aucun pattern trouve.
 * Scanne toutes les fenetres de 5 cases possibles autour de (x,y).
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

/**
 * Convertit un score numerique en niveau de menace categorique.
 * 
 * Niveaux :
 * - IDX_WIN : Victoire immediate (5 alignes)
 * - IDX_OPEN_FOUR : Menace imparable (4 alignes ouverts ou gapped four)
 * - IDX_CLOSED_FOUR : Menace forte (4 alignes fermes)
 * - IDX_OPEN_THREE : Menace serieuse (3 alignes ouverts)
 * - IDX_CLOSED_THREE : Menace faible (3 alignes fermes)
 * - IDX_OTHERS : Autres patterns
 */
int get_threat_level(int score) {
    if (score >= WIN_SCORE) return IDX_WIN;
    if (score >= OPEN_FOUR) return IDX_OPEN_FOUR; // Inclut les Gapped Fours gagnants
    if (score >= CLOSED_FOUR) return IDX_CLOSED_FOUR; // Inclut les menaces fortes
    if (score >= OPEN_THREE) return IDX_OPEN_THREE;
    if (score >= CLOSED_THREE) return IDX_CLOSED_THREE;
    return IDX_OTHERS;
}

/**
 * Recalcule completement les statistiques du plateau.
 * 
 * Reinitialise puis scanne toutes les pierres pour mettre a jour :
 * - Scores positionnels incrementaux
 * - Compteurs de menaces par niveau
 * - Niveau de menace maximum par joueur
 * 
 * A appeler apres une modification majeure du plateau ou au debut d'un tour.
 */
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
/**
 * Trouve la case critique d'un gapped four pour un joueur.
 * 
 * Un gapped four est un pattern X_XXX ou XXX_X qui devient victoire si le trou est comble.
 * Scanne tout le plateau a la recherche de cases vides qui completent un tel pattern.
 * 
 * Retourne l'index de la case critique, ou -1 si aucun gapped four detecte.
 */
int find_gapped_four_hole(game *g, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    // Optimisation : On ne scanne que les cases vides.
    // Si une case vide est entourée de 4 pierres alliées dans une direction, c'est une victoire.
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        // Petit filtre rapide : si la case n'a aucun voisin immédiat, inutile de scanner les lignes
        // (Optionnel, mais peut gagner du temps en début de partie)
        /* bool has_neighbor = false;
        for (int i = 0; i < 4; i++) {
             // check rapide dx/dy...
        }
        if (!has_neighbor) continue; 
        */

        for (int d = 0; d < 4; d++) {
            int stones = 0;
            
            // Scan dans les deux sens (Positif et Négatif)
            // On utilise une boucle pour éviter la duplication de code
            for (int dir = -1; dir <= 1; dir += 2) {
                for (int k = 1; k <= 4; k++) {
                    int nx = x + dx[d] * k * dir;
                    int ny = y + dy[d] * k * dir;
                    
                    if (!IS_VALID(nx, ny)) break;
                    
                    if (g->board[GET_INDEX(nx, ny)] == player) {
                        stones++;
                    } else {
                        break; // Pierre adverse ou vide : la ligne s'arrête
                    }
                }
            }
            
            // Si le total des pierres connectées (plus celle qu'on poserait) ferait 5 ou plus
            // Note : stones est la somme des voisins. Si stones >= 4, alors stones + 1 (le coup) == 5.
            if (stones >= 4) {
                return idx;
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

/*
 * Détecte si un coup crée une fourchette (plusieurs menaces simultanées)
 * Un bonus est accordé si le coup appartient à plusieurs lignes prometteuses.
 */
int compute_fork_bonus(game *g, int x, int y, int player) {
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int threats_count = 0;
    int bonus = 0;

    for (int d = 0; d < 4; d++) {
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        
        // On compte les menaces créées ou complétées par ce coup
        if (score >= OPEN_TWO) {
            threats_count++;
        }
        // Un coup qui prépare un Open Three est très fort
        if (score >= OPEN_THREE) {
            bonus += 5000;
        }
    }

    // Si le coup crée une intersection de 2 menaces ou plus
    if (threats_count >= 2) {
        return 15000 + bonus; // Bonus de fourchette
    }
    return bonus;
}

// On veut que l'IA privilégie les cases qui appartiennent à plusieurs lignes
static int get_intersection_bonus(game *g, int x, int y, int player) {
    int active_lines = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int d = 0; d < 4; d++) {
        // Si la ligne dans cette direction a au moins une pierre du joueur
        // et aucune pierre adverse, elle est "active".
        int p_count = 0;
        int o_count = 0;
        int opponent = (player == P1) ? P2 : P1;
        
        for (int k = -4; k <= 4; k++) {
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (IS_VALID(nx, ny)) {
                if (g->board[GET_INDEX(nx, ny)] == player) p_count++;
                else if (g->board[GET_INDEX(nx, ny)] == opponent) o_count++;
            }
        }
        if (p_count > 0 && o_count == 0) active_lines++;
    }
    return (active_lines > 1) ? (active_lines * 500) : 0;
}

int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
    int score = 0;
    int cells[6]; 
    int opponent = (player == P1) ? P2 : P1;
    
    for(int i=1; i<6; i++) {
        int nx = x + dx*i;
        int ny = y + dy*i;
        if (IS_VALID(nx, ny)) cells[i] = g->board[GET_INDEX(nx, ny)];
        else cells[i] = opponent; 
    }

    int len = 1;
    while(len < 6 && cells[len] == player) len++;
    
    if (len >= 5) return WIN_SCORE;
    
    int start_idx = GET_INDEX(x - dx, y - dy);
    bool start_open = IS_VALID(x-dx, y-dy) && g->board[start_idx] == EMPTY;
    bool end_open = (len < 6 && cells[len] == EMPTY);

    if (len == 4) {
        if (start_open && end_open) return OPEN_FOUR; // 10M
        if (start_open || end_open) return CLOSED_FOUR; // 5M
    }
    else if (len == 3) {
        if (start_open && end_open) return OPEN_THREE; // 2M
        if (start_open || end_open) return CLOSED_THREE; // 50k
    }
    else if (len == 2) {
        if (start_open && end_open) return OPEN_TWO; // 1k
        if (start_open || end_open) return CLOSED_TWO; // 100
    }

    // --- DETECTION DES TROUS (Gapped Patterns) ---
    // Pattern X.XXX ou XXX.X (Gapped Four)
    if (len < 4) {
        if (cells[1] == EMPTY && cells[2] == player && cells[3] == player && cells[4] == player) {
            return (start_open) ? OPEN_FOUR : CLOSED_FOUR;
        }
        if (cells[1] == player && cells[2] == EMPTY && cells[3] == player && cells[4] == player) {
            return CLOSED_FOUR;
        }
    }

    return score;
}

// Aide à évaluer une ligne complète passant par (x,y)
static int evaluate_full_line(game *g, int x, int y, int dx, int dy, int player) {
    int count = 1; // La pierre qu'on simule en (x,y)
    int open_ends = 0;
    int opponent = (player == P1) ? P2 : P1;

    // On regarde vers l'avant
    for (int i = 1; i <= 4; i++) {
        int nx = x + dx * i, ny = y + dy * i;
        if (!IS_VALID(nx, ny) || g->board[GET_INDEX(nx, ny)] == opponent) break;
        if (g->board[GET_INDEX(nx, ny)] == player) count++;
        else { open_ends++; break; } // Case vide
    }
    // On regarde vers l'arrière
    for (int i = 1; i <= 4; i++) {
        int nx = x - dx * i, ny = y - dy * i;
        if (!IS_VALID(nx, ny) || g->board[GET_INDEX(nx, ny)] == opponent) break;
        if (g->board[GET_INDEX(nx, ny)] == player) count++;
        else { open_ends++; break; } // Case vide
    }

    if (count >= 5) return WIN_SCORE;
    if (count == 4) return (open_ends == 2) ? OPEN_FOUR : CLOSED_FOUR;
    if (count == 3) return (open_ends == 2) ? OPEN_THREE : CLOSED_THREE;
    if (count == 2) return (open_ends == 2) ? OPEN_TWO : CLOSED_TWO;
    return 0;
}

int get_point_score(game *g, int x, int y, int player) {
    int total = 0;
    int max_line = 0;
    int opponent = (player == P1) ? P2 : P1;
    
    // 1. Évaluation directionnelle classique
    int d_scores[4];
    d_scores[0] = evaluate_full_line(g, x, y, 1, 0, player);
    d_scores[1] = evaluate_full_line(g, x, y, 0, 1, player);
    d_scores[2] = evaluate_full_line(g, x, y, 1, 1, player);
    d_scores[3] = evaluate_full_line(g, x, y, 1, -1, player);

    for (int i = 0; i < 4; i++) {
        if (d_scores[i] >= WIN_SCORE) return WIN_SCORE;
        total += d_scores[i];
        if (d_scores[i] > max_line) max_line = d_scores[i];
    }

    // 2. BONUS DE FOURCHETTE (Intersection)
    total += compute_fork_bonus(g, x, y, player);

    // 3. AMÉLIORATION PHASE 5 : Rendre l'évaluation TRÈS AGRESSIVE
    // Multiplier les menaces offensives par 1.5 pour favoriser l'attaque
    if (max_line >= OPEN_THREE) {
        total = (int)(total * 1.5); // Augmenté de 1.25 à 1.5
    } else if (max_line >= CLOSED_THREE) {
        total = (int)(total * 1.3); // Bonus même pour menaces moyennes
    }

    // 4. NOUVEAU : BONUS SETUP FOURCHETTES (Préparation)
    // Détecter les coups qui préparent une fourchette au prochain tour
    int setup_bonus = 0;
    int potential_threats = 0;
    
    // Simuler le coup
    g->board[GET_INDEX(x, y)] = player;
    
    // Chercher les cases adjacentes qui créeraient une fourchette après ce coup
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (!IS_VALID(nx, ny) || g->board[GET_INDEX(nx, ny)] != EMPTY) continue;
            
            // Simuler ce deuxième coup
            g->board[GET_INDEX(nx, ny)] = player;
            int threats = count_created_threats(g, GET_INDEX(nx, ny), player);
            g->board[GET_INDEX(nx, ny)] = EMPTY;
            
            if (threats >= 2) {
                potential_threats++;
            }
        }
    }
    
    g->board[GET_INDEX(x, y)] = EMPTY;
    
    if (potential_threats >= 2) {
        setup_bonus = 500000; // GROS bonus pour setup fourchette
    } else if (potential_threats == 1) {
        setup_bonus = 200000; // Bonus moyen
    }
    total += setup_bonus;

    // 5. BONUS D'INITIATIVE (cases centrales en début de partie)
    int stone_count = 0;
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) stone_count++;
    }
    
    if (stone_count < 20) { // Début de partie
        int center_dist = abs(x - 9) + abs(y - 9);
        if (center_dist <= 4) {
            total += (5 - center_dist) * 200; // Bonus jusqu'à 1000
        }
    }

    // 6. BONUS PROSPECTIF : Détecter les menaces latentes (simplifié)
    // Un coup qui prépare de futures menaces (pierres espacées mais alignables)
    int latent_threats = 0;
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};
    
    for (int d = 0; d < 4; d++) {
        // Compter les pierres alliées dans un rayon de 2-3 cases (réduit pour perf)
        int stones_in_range = 0;
        for (int k = -3; k <= 3; k++) {
            if (k == 0) continue;
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] == player) {
                stones_in_range++;
            }
        }
        if (stones_in_range >= 2) latent_threats++;
    }
    
    if (latent_threats >= 2) {
        total += latent_threats * 400; // Augmenté de 300 à 400
    }

    return total;
}

/*
 * NOUVEAU : Évaluation ULTRA-LÉGÈRE pour le VCF
 * Compte simplement les pierres alignées sans calculs lourds
 */
int get_point_score_fast(game *g, int x, int y, int player) {
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};
    int max_score = 0;
    
    for (int d = 0; d < 4; d++) {
        int stones = 1; // La pierre qu'on pose
        int open_ends = 0;
        
        // Compter vers le positif
        for (int k = 1; k <= 4; k++) {
            int nx = x + dx[d] * k, ny = y + dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            if (g->board[GET_INDEX(nx, ny)] == player) stones++;
            else if (g->board[GET_INDEX(nx, ny)] == EMPTY) { open_ends++; break; }
            else break;
        }
        
        // Compter vers le négatif
        for (int k = 1; k <= 4; k++) {
            int nx = x - dx[d] * k, ny = y - dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            if (g->board[GET_INDEX(nx, ny)] == player) stones++;
            else if (g->board[GET_INDEX(nx, ny)] == EMPTY) { open_ends++; break; }
            else break;
        }
        
        // Scoring rapide
        int score = 0;
        if (stones >= 5) score = WIN_SCORE;
        else if (stones == 4) score = (open_ends == 2) ? OPEN_FOUR : CLOSED_FOUR;
        else if (stones == 3) score = (open_ends == 2) ? OPEN_THREE : CLOSED_THREE;
        else if (stones == 2) score = (open_ends == 2) ? OPEN_TWO : CLOSED_TWO;
        
        if (score > max_score) max_score = score;
    }
    
    return max_score;
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

// Ajoute cette fonction helper
int count_overlapping_threats(game *g, int player) {
    int threats = 0;
    // On parcourt les lignes pré-calculées par ai_data.c (ou on scanne léger)
    // Ici, une heuristique simplifiée : 
    // On regarde les cases vides qui ont plus d'une ligne active pour le joueur.
    
    // NOTE : Cela suppose que tu as accès aux données 'lines' ou que tu scannes.
    // Si tu n'as pas de structure complexe, on va faire un scan heuristique rapide 
    // sur les coups possibles de l'adversaire.
    
    // Version simplifiée : On compte combien de Open Threes l'adversaire a.
    // Si > 1, c'est une fourchette potentielle.
    int open_threes = 0;
    int blocked_fours = 0; // Menace Closed Four
    
    // On utilise les données que tu as déjà dans g->max_threat_level ?
    // Non, max_threat_level donne juste le MAX. On a besoin du COMPTE.
    
    // Il faudrait idéalement une fonction qui retourne le NOMBRE de menaces de niveau 3/4.
    // Si cette info n'est pas dispo dans 'game', on l'estime via le score positionnel
    // ou on modifie ai_data.c pour compter les menaces.
    
    return 0; // Placeholder si pas implémentable facilement
}

/*
 * Nouvelle version O(1) de evaluate_board
 * Elle utilise les valeurs pré-calculées.
 */
int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // PHASE 5 : OFFENSIVE MAXIMALE AVEC DÉFENSE INTELLIGENTE
    long long my_score = g->pos_score[player];
    long long opp_score = g->pos_score[opponent];
    
    // Bonus captures
    my_score += g->captures[player] * CAPTURE_BONUS;
    opp_score += g->captures[opponent] * CAPTURE_BONUS;
    
    // Victoire par capture
    if (g->captures[player] >= 5) return WIN_SCORE;
    if (g->captures[opponent] >= 5) return -WIN_SCORE;
    
    // Compter les pierres pour détecter l'ouverture
    int stone_count = 0;
    for (int i = 0; i < MAX_BOARD; i++)
        if (g->board[i] != EMPTY) stone_count++;
    
    // STRATÉGIE ADAPTATIVE:
    // - Ouverture: Ultra-agressif (construire avantage)
    // - Mid-game: Agressif mais défense sur menaces critiques
    
    if (stone_count < 15) {
        // OUVERTURE: Priorité absolue à l'offensive
        my_score = (long long)(my_score * 5.0);
        opp_score = (long long)(opp_score * 0.5);
    } else {
        // MID-GAME: Défense adaptative selon niveau de menace
        
        // Offensive: toujours boostée
        my_score = (long long)(my_score * 4.0);
        
        // Défense: Dépend du danger adverse
        double def_multiplier = 0.6; // Défaut: basse priorité
        
        // EXCEPTION CRITIQUE: Menaces mortelles DOIVENT être bloquées
        // Si adversaire a OPEN_FOUR ou WIN_SCORE, défense = priorité absolue
        if (g->max_threat_level[opponent] >= IDX_OPEN_FOUR) {
            def_multiplier = 3.0; // Défense > Offensive pour survie
        } else if (g->max_threat_level[opponent] >= IDX_CLOSED_FOUR) {
            def_multiplier = 1.5; // Menace sérieuse, attention requise
        } else if (g->max_threat_level[opponent] >= IDX_OPEN_THREE) {
            def_multiplier = 0.8; // Légère attention
        }
        
        opp_score = (long long)(opp_score * def_multiplier);
    }
    
    long long total = my_score - opp_score;
    
    // Sécurité overflow
    if (total > INT_MAX) return INT_MAX;
    if (total < INT_MIN) return INT_MIN;
    
    return (int)total;
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

bool is_double_three(game *g, int idx, int player) {

    int x = GET_X(idx);
    int y = GET_Y(idx);

    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    g->board[idx] = player;
    
    int open_three_count = 0;
    
    for (int d = 0; d < 4; d++) {
        int score = evaluate_line(g, x, y, dx[d], dy[d], player);
        
        // STRICTEMENT Open Three (pas Four)
        if (score == OPEN_THREE) {
            open_three_count++;
        }
    }
    
    g->board[idx] = EMPTY;
    
    // Exception: Capture annule l'interdiction
    if (open_three_count >= 2) {
        g->board[idx] = player;
        int caps = count_potential_captures(g, x, y, player);
        g->board[idx] = EMPTY;
        return (caps == 0); // Autorisé si capture
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
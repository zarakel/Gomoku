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

/* Forward declarations : définies plus bas dans ce fichier */
static void update_stats(game *g, int player, int score, bool remove_mode);
static int evaluate_full_line(game *g, int x, int y, int dx, int dy, int player);

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

    // 2. Scan correct : pour chaque direction, on trouve les TÊTES de lignes
    // uniques et on évalue chacune UNE SEULE FOIS.
    //
    // BUG CORRIGÉ : l'ancienne version appelait update_impacted_scores() pour
    // chaque pierre, ce qui réévaluait la même tête de ligne autant de fois
    // qu'il y a de pierres dans son rayon d'impact (k=-5..0).
    // Exemple : 3 pierres consécutives P1 → la ligne de tête était comptée 3×
    // dans threat_counts, provoquant des faux positifs WIN_SCORE (Score: 2000000X)
    // alors que la position n'était pas terminale.
    //
    // Correction : on itère par direction, on ne traite que les starts
    // (case précédente vide/hors-plateau) → chaque ligne est comptée exactement 1×.
    int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];

        for (int i = 0; i < MAX_BOARD; i++) {
            int p = g->board[i];
            if (p == EMPTY) continue;

            int x = GET_X(i);
            int y = GET_Y(i);

            // Ne traiter que le début d'une séquence pour ce joueur dans cette direction
            int prev_x = x - dx;
            int prev_y = y - dy;
            bool is_start = !IS_VALID(prev_x, prev_y)
                            || g->board[GET_INDEX(prev_x, prev_y)] != p;

            if (is_start) {
                int score = evaluate_full_line(g, x, y, dx, dy, p);
                update_stats(g, p, score, false); // ADD — exactement 1× par ligne
            }
        }
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

    // D5-FIX: Seule la Méthode 1 (scan depuis les pierres du joueur) est nécessaire.
    // find_gapped_three_in_line construit un buffer centré sur (x,y) et cherche
    // les patterns .X_XX. et .XX_X. dans toutes les fenêtres → couvre tous les cas.
    // La Méthode 2 (scan depuis les cases vides) détectait les mêmes trous mais
    // en arrivant du côté opposé → double comptage, O(2×MAX_BOARD×4) au lieu de O(MAX_BOARD×4).
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
        // evaluate_full_line : bidirectionnel — détecte les forks où le coup est
        // au centre (ex: X * X en diagonal), que evaluate_line (unidirectionnel) ratait.
        int score = evaluate_full_line(g, x, y, dx[d], dy[d], player);
        
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

    // 3. BONUS DE COMBINAISON DIRECTIONNELLE
    // Remplace les multiplicateurs ×1.5/×1.3 qui gonflaient artificiellement les scores
    // et causaient des faux positifs OPEN_FOUR dans count_immediate_threats (par ex:
    // OPEN_THREE(2M) + CLOSED_FOUR(5M) = 7M × 1.5 = 10.5M ≥ OPEN_FOUR → faux positif).
    // On remplace par un bonus explicite quand plusieurs directions sont actives :
    // chaque direction ≥ OPEN_THREE en plus de la première ajoute OPEN_THREE/2 = 1M.
    // Exemple : 2 OPEN_THREE = 4M + 1M = 5M (< OPEN_FOUR ✓). 1 OPEN_THREE + 1 CLOSED_FOUR
    // → max_line=CLOSED_FOUR donc pas de combo → valeur brute 7M (< OPEN_FOUR ✓).
    {
        int open_three_dirs = 0;
        for (int i = 0; i < 4; i++) {
            if (d_scores[i] >= OPEN_THREE) open_three_dirs++;
        }
        if (open_three_dirs >= 2) {
            total += (open_three_dirs - 1) * (OPEN_THREE / 2); // +1M par direction supplémentaire
        }
    }

    // 4. BONUS PROSPECTIF : Détecter les menaces latentes (simplifié)
    // NOTE: Le bonus "setup fourchette" (simulation imbriquée 5×5 × count_created_threats)
    // et le scan stone_count O(361) ont été supprimés : ils représentaient ~100 évaluations
    // de lignes par appel (×2 par move, ×50 moves, ×milliers de noeuds).
    // Le bonus fourchette direct est déjà couvert par compute_fork_bonus() ci-dessus
    // et par compute_fork_value() dans score_move_ordering().
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

        // FIX: Fenêtre étendue de [-5, +1] autour de (x,y).
        // k=-5..0 : couvre les têtes de lignes EN AMONT qui passent par (x,y).
        // k=+1    : couvre la tête de ligne IMMÉDIATEMENT EN AVAL.
        //   Quand on POSE une pierre à (x,y) :
        //     - La case (x+dx, y+dy) pouvait être un « start » (board[x]=EMPTY ≠ p).
        //       Après la pose, si board[x]=p, ce n'est plus un start.
        //       → Son ancien score doit être retiré (step remove) mais pas re-ajouté
        //         (step add la skippera car prev == p).
        //   Quand on RETIRE une pierre de (x,y) :
        //     - La case (x+dx, y+dy) redevient un start (board[x]=EMPTY ≠ p).
        //       → Son nouveau score doit être ajouté (step add).
        //   Sans k=+1, ces scores « forward » restaient stale dans pos_score
        //   et threat_counts (ex: IDX_WIN false positive observé en test).
        for (int k = -5; k <= 1; k++) {
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
                // evaluate_full_line : bidirectionnel depuis la tête de ligne.
                // evaluate_line (unidirectionnel) sous-évaluait les pierres au centre
                // d'un alignement car il ne voyait qu'une moitié de la séquence.
                // Ex : X au centre de .X*X. → evaluate_line voit len=1, evaluate_full_line voit 3.
                // Les deux doivent rester cohérents avec get_point_score / score_move_ordering.
                int score = evaluate_full_line(g, sx, sy, dx, dy, p);
                update_stats(g, p, score, remove_mode);
            }
        }
    }
}


/*
 * evaluate_board - Évaluation symétrique de la position.
 *
 * RÈGLE FONDAMENTALE NEGAMAX : eval(pos, P1) == -eval(pos, P2)
 * Sans cette symétrie, toute l'arborescence de recherche est corrompue.
 *
 * Structure :
 *   1. Terminaux stricts  : victoire/défaite réelle → ±WIN_SCORE
 *   2. Évaluation nette   : my_pos - opp_pos (score brut symétrique)
 *   3. Plafonnement       : jamais ≥ WIN_SCORE-1000, pour ne pas
 *                           déclencher la détection terminale dans negamax
 *                           sur une position non-gagnée.
 *
 * L'agressivité (biais offensif/défensif) est gérée dans score_move_ordering,
 * pas ici. evaluate_board doit rester neutre et symétrique.
 */
int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;

    // 1. TERMINAUX : victoire par capture
    if (g->captures[player] >= 5)   return  WIN_SCORE;
    if (g->captures[opponent] >= 5) return -WIN_SCORE;

    // 2. TERMINAUX : victoire par alignement de 5
    // threat_counts[p][IDX_WIN] est maintenu à jour par update_stats()
    // à chaque apply_move / undo_move via update_impacted_scores().
    if (g->threat_counts[player][IDX_WIN] > 0)   return  WIN_SCORE;
    if (g->threat_counts[opponent][IDX_WIN] > 0) return -WIN_SCORE;

    // 3. QUASI-TERMINAUX : double Open Four
    // Si l'adversaire a ≥2 Open Fours simultanés, la position est perdue :
    // on ne peut bloquer qu'une menace par coup. Retourner immédiatement le
    // score plancher pour que negamax fuit cette branche au plus vite.
    // Symétrique : si c'est nous qui avons ≥2 Open Fours, position gagnante.
    int opp_open_fours = g->threat_counts[opponent][IDX_OPEN_FOUR];
    int my_open_fours  = g->threat_counts[player][IDX_OPEN_FOUR];
    if (opp_open_fours >= 2) return -(WIN_SCORE - 1001);
    if (my_open_fours  >= 2) return  (WIN_SCORE - 1001);

    // 3b. QUASI-TERMINAUX : double Closed Four
    // Un Closed Four = 4 pierres avec 1 extrémité ouverte seulement.
    // Un seul coup de cet adversaire transforme chaque Closed Four en victoire (si l'extrémité
    // ouverte est libre). Avec 2+ Closed Fours simultanés : on ne peut bloquer qu'un seul.
    // C'est la menace qui précède d'un coup le double Open Four — le vrai signal précoce.
    int opp_closed_fours = g->threat_counts[opponent][IDX_CLOSED_FOUR];
    int my_closed_fours  = g->threat_counts[player][IDX_CLOSED_FOUR];
    if (opp_closed_fours >= 2) return -(WIN_SCORE - 2001);
    if (my_closed_fours  >= 2) return  (WIN_SCORE - 2001);

    // 3b2. QUASI-TERMINAUX : Open Three + Closed Four adverse (combo fourchette imminente)
    // 1 Open Three + 1 Closed Four = l'adversaire peut jouer un coup qui crée
    // SIMULTANÉMENT un Open Four depuis le Closed Four ET prolonge l'Open Three —
    // résultat : 2 Open Fours en 1 coup (fourchette irrémédiable).
    // Observé dans les logs : AI score +20M à D8, adversaire joue ce combo → CRISE 3.
    // Ce quasi-terminal (-WIN_SCORE+2500) est plus urgent que double Closed Four
    // seul (-WIN_SCORE+2001 = légèrement moins grave car 2 coups nécessaires).
    // Seuil 2500 > 2001 : placé ENTRE double-closed-four et double-open-four
    // dans la hiérarchie, correctement en-dessous de double-open-four (1001).
    int opp_open_threes_pre = g->threat_counts[opponent][IDX_OPEN_THREE];
    int my_open_threes_pre  = g->threat_counts[player][IDX_OPEN_THREE];
    if (opp_closed_fours >= 1 && opp_open_threes_pre >= 1) return -(WIN_SCORE - 2500);
    if (my_closed_fours  >= 1 && my_open_threes_pre  >= 1) return  (WIN_SCORE - 2500);

    // 3c. QUASI-TERMINAUX : 4 captures adverses
    // 4 paires capturées = l'adversaire gagne dès qu'il capture 1 paire de plus.
    // Toute case exposée en fourchette devient un coup gagnant immédiat par capture.
    // CAPTURE_BONUS linéaire (50K/paire) est insuffisant : 4×50K=200K est invisible
    // face aux menaces d'alignement (OPEN_THREE=2M). Ce quasi-terminal corrige ça.
    // Hiérarchie : open_fours≥2 > closed_fours≥2 > caps≥4 > caps≥3
    int opp_caps = g->captures[opponent];
    int my_caps  = g->captures[player];
    if (opp_caps >= 4) return -(WIN_SCORE - 3001);
    if (my_caps  >= 4) return  (WIN_SCORE - 3001);

    // 3d. QUASI-TERMINAUX : 3 captures + ≥1 open four adverse
    // L'adversaire à 3 paires capturées + ≥1 open four = quasi-perdu :
    // son open four force soit un alignement (si on ne bloque pas) soit une
    // capture supplémentaire (si on bloque mais expose une paire). Légèrement
    // moins urgent que caps≥4 seul → -(WIN_SCORE-3501) > -(WIN_SCORE-3001).
    if (opp_caps >= 3 && opp_open_fours >= 1) return -(WIN_SCORE - 3501);
    if (my_caps  >= 3 && my_open_fours  >= 1) return  (WIN_SCORE - 3501);

    // 4. ÉVALUATION SYMÉTRIQUE
    // pos_score accumule tous les scores de ligne ; on ajoute le bonus capture.
    long long my_score  = g->pos_score[player]   + (long long)g->captures[player]   * CAPTURE_BONUS;
    long long opp_score = g->pos_score[opponent] + (long long)g->captures[opponent] * CAPTURE_BONUS;

    long long total = my_score - opp_score;

    // 4a2. MALUS Closed Four isolé (1 closed four sans open three)
    // Un closed four seul est souvent invisible dans le score brut car il vaut
    // seulement CLOSED_FOUR=5M dans pos_score — et pos_score est brut (somme de toutes
    // les lignes). En pratique, l'adversaire peut promouvoir en open four en 1 coup.
    // L'open_four promotion déclenche opp_open_fours>=1 (CRISE 2) mais c'est TROP TARD :
    // le beam crisis (D6) ne voit pas assez loin pour bloquer + contre-attaquer.
    // Pénalité explicite OPEN_FOUR (10M) quand opp a 1 closed four sans combo connu :
    // force minimax à traiter la menace AVANT qu'elle se transforme en fourchette.
    // Guard : seulement si pas de combo open_three (déjà géré par quasi-terminal 3b2).
    if (opp_closed_fours >= 1 && opp_open_threes_pre == 0) {
        total -= (long long)OPEN_FOUR;
    }
    if (my_closed_fours >= 1 && my_open_threes_pre == 0) {
        total += (long long)OPEN_FOUR;
    }

    // 4b. MALUS DE PRÉ-FOURCHETTE ADVERSE (niveau Open Three)
    // Si l'adversaire a déjà 2+ Open Threes actifs, la position est structurellement
    // dangereuse même si le score brut semble équilibré. On pénalise pour que
    // minimax préfère les branches où l'adversaire n'a pas encore construit ça.
    int opp_open_threes = opp_open_threes_pre;
    if (opp_open_threes >= 2) {
        total -= (long long)(opp_open_threes - 1) * OPEN_FOUR;
    }
    // Symétriquement : bonus si c'est nous qui avons plusieurs Open Threes
    int my_open_threes = my_open_threes_pre;
    if (my_open_threes >= 2) {
        total += (long long)(my_open_threes - 1) * OPEN_FOUR;
    }

    // 4c. MALUS PRÉ-FOURCHETTE : Open Three + Closed Four
    // Désormais géré par quasi-terminal 3b2 (retour anticipé).
    // Ces cas ne devraient plus atteindre cette section du code.
    // Conservé comme garde de sécurité uniquement.
    // (intentionnellement vide : le quasi-terminal rend ce calcul redondant)

    // 4d. MALUS DOUBLE-EXTENSION LATENTE (2+ Closed Threes en directions distinctes)
    int opp_closed_threes = g->threat_counts[opponent][IDX_CLOSED_THREE];
    int my_closed_threes  = g->threat_counts[player][IDX_CLOSED_THREE];
    if (opp_closed_threes >= 2) total -= (long long)(opp_closed_threes - 1) * OPEN_THREE;
    if (my_closed_threes  >= 2) total += (long long)(my_closed_threes  - 1) * OPEN_THREE;

    // 4d3. MALUS RÉSEAU MULTI-CLOSED-THREE (3+ closed threes simultanés)
    // 3+ closed_threes = réseau convergent précurseur de 4+ open_fours en 2 coups.
    // Observé dans logs partie 3 : l'IA score +28K pendant que l'humain construit
    // ce réseau, puis explosion à 4 open_fours. Le malus 4d (2× OPEN_THREE = 4M)
    // est compensé par le pos_score offensif → balance vue comme +28K.
    // Pénalité additionnelle OPEN_FOUR (10M) pour les cas >= 3 : rend la position
    // quasi-terminale pour minimax qui préférera bloquer la construction.
    // Guard : seulement si aucun open_four adverse déjà présent (quasi-terminal 3a déjà actif).
    if (opp_closed_threes >= 3 && opp_open_fours == 0)
        total -= (long long)OPEN_FOUR;
    if (my_closed_threes  >= 3 && my_open_fours  == 0)
        total += (long long)OPEN_FOUR;

    // 4d3. NOTE : le malus "réseau multi-fork latent" (opp_open_threes>=1 + opp_closed_threes>=2)
    // a été testé à plusieurs reprises (OPEN_FOUR=10M, WIN_SCORE-4001 quasi-terminal,
    // OPEN_THREE*3=6M) et cause systématiquement des régressions :
    // - Valeur trop forte : franchit le plafonnement WIN_SCORE-1001 en cumul avec 4d+4d2
    // - Valeur trop faible : invisible face à un score offensif de +20M
    // - Asymétrique mais garde (stone_count>=10) insuffisante : déclenche sur positions normales
    // La protection contre ce pattern est correctement gérée par :
    //   ai.c : WIN-OVERRIDE SUPPRESSED (opp_network guard)
    //   ai_moves.c : beam extension network_threat (+1/+2 cap 12/14)
    // Ces mécanismes opèrent au niveau de la recherche (pas du scoring statique) et
    // n'ont pas d'effets de bord sur l'aspiration search ni sur le pruning.

    // 4d2. MALUS PRÉ-FOURCHETTE COMBINÉ : Open Two + Closed Three adverses simultanés
    // 2+ OPEN_TWO seuls = paires isolées, peut être notre propre construction offensive.
    // Le pénaliser symétriquement bruitait les positions offensives légitimes (régression
    // observée : l'IA abandonnait ses propres structures en cours).
    // Condition stricte : OPEN_TWO >= 2 ET CLOSED_THREE >= 1 simultanément = l'adversaire
    // a des paires EN COURS de devenir des menaces, pas juste 2 paires isolées.
    // Pénalité ciblée OPEN_THREE (2M) : visible mais n'écrase pas les terminaux.
    {
        int opp_open_twos    = g->threat_counts[opponent][IDX_OPEN_TWO];
        int opp_cls_threes   = g->threat_counts[opponent][IDX_CLOSED_THREE];
        if (opp_open_twos >= 2 && opp_cls_threes >= 1)
            total -= (long long)OPEN_THREE;
        // Symétrique offensif : bonus si c'est nous qui combinons paires + menaces fermées
        int my_open_twos   = g->threat_counts[player][IDX_OPEN_TWO];
        int my_cls_threes  = g->threat_counts[player][IDX_CLOSED_THREE];
        if (my_open_twos >= 2 && my_cls_threes >= 1)
            total += (long long)OPEN_THREE;
    }

    // 4e. MALUS CAPTURES AVANCÉES (3 paires)
    // 4 captures = quasi-terminal (retour anticipé section 3c).
    // 3 captures = 1 seule paire de plus suffit à mettre l'adversaire à 4 (quasi-terminal).
    // Pénalité doublée (2×OPEN_FOUR = 20M) : le CAPTURE_BONUS linéaire (3×50K=150K) est
    // totalement invisible face aux scores offensifs (OPEN_FOUR=10M, OPEN_THREE=2M).
    // Sans cette pénalité forte, minimax ne voit pas de différence entre une position
    // à 3 captures adverses exposée et une position sûre → continue de jouer offensif
    // en offrant la 4e paire. Ce bug causait la défaite G1 (open four recréé 3 fois).
    if (opp_caps >= 3) total -= (long long)OPEN_FOUR * 2;
    if (my_caps  >= 3) total += (long long)OPEN_FOUR * 2;

    // 4. PLAFONNEMENT STRICT
    // Empêche une position non-terminale (ex: 2 open fours = 20M pts)
    // d'être confondue avec une vraie victoire par negamax.
    if (total >=  (WIN_SCORE - 1000)) total =  (WIN_SCORE - 1001);
    if (total <= -(WIN_SCORE - 1000)) total = -(WIN_SCORE - 1001);

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
    // Seul P1 (Noir) est soumis à la règle du double-three (Renju).
    // P2 n'a aucune restriction → retour immédiat O(1) pour éviter
    // 4×check_free_three_pattern inutiles dans les noeuds quiescence P2.
    if (player != P1) return false;

    int x = GET_X(idx);
    int y = GET_Y(idx);

    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    // B4-FIX : utiliser check_free_three_pattern au lieu de evaluate_full_line.
    // evaluate_full_line compte TOUTES les pierres dans la direction, y compris
    // celles qui forment un three PRÉEXISTANT sans le coup en idx.
    // Exemple : . X X . (3 pierres consécutives déjà là) → evaluate_full_line voit
    // OPEN_THREE même si le coup en idx est à 3 cases de distance et n'y participe pas.
    // check_free_three_pattern vérifie que idx fait PARTIE du pattern détecté → correct.
    int open_three_count = 0;
    
    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            open_three_count++;
        }
    }
    
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
#include "../include/gomoku.h"

// Tables de score (Rappel : Open 4 et Closed 4 sont des menaces mortelles)
static const int SCORE_TABLE[6][3] = {
    {0, 0, 0},             
    {1, 10, 15},           
    {10, 2000, 8000},         
    {100, 250000, 500000},     
    {6000, 250000, 500000},  
    {WIN_SCORE, WIN_SCORE, WIN_SCORE} 
};

// Helper inline pour éviter la répétition de code
static inline int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
    int score = 0;
    int len = 1;
    int open_ends = 0;
    
    // Scan Positif
    int i = 1;
    for (; i < 5; i++) {
        int nx = x + dx * i;
        int ny = y + dy * i;
        if (nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) break;
        int cell = g->board[ny * BOARD_SIZE + nx]; // Optimisation index manuel
        if (cell == player) len++;
        else if (cell == EMPTY) { open_ends++; break; } 
        else break; 
    }

    // Scan Négatif
    int j = 1;
    for (; j < 5; j++) {
        int nx = x - dx * j;
        int ny = y - dy * j;
        if (nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) break;
        int cell = g->board[ny * BOARD_SIZE + nx];
        if (cell == player) len++;
        else if (cell == EMPTY) { open_ends++; break; }
        else break;
    }

    // Scoring Continu
    if (len >= 5) return WIN_SCORE;
    if (len == 4) {
        if (open_ends >= 1) score += 20000000; // Open 4 (Mortel)
        else {
            // Closed 4 (_ X X X X O)
            // AVANT : 150000 -> MAINTENANT : 250000
            // C'est plus qu'un Open 3 (100000). L'IA ne laissera plus jamais passer ça.
            score += 250000; 
        }
    }
    else if (len == 3) {
        if (open_ends == 2) score += OPEN_THREE; // 100 000
        else if (open_ends == 1) score += 80000; // Closed 3
    }
    else if (len == 2) {
        if (open_ends == 2) score += OPEN_TWO;
    }

    // Scoring Gap (Simplifié pour la vitesse)
    if (len < 4) {
        // On vérifie juste le motif X _ X X ou X X _ X
        // C'est le motif le plus courant de Broken 3/4
        int px = x + dx, py = y + dy;
        int mx = x - dx, my = y - dy;
        
        // Check Gap Positif (X _ X ...)
        if (IS_VALID(px, py) && g->board[py*BOARD_SIZE+px] == EMPTY) {
             int ppx = px + dx, ppy = py + dy;
             if (IS_VALID(ppx, ppy) && g->board[ppy*BOARD_SIZE+ppx] == player) {
                 // On a X _ X. Si on a encore un X derrière, c'est fort.
                 score += 1000; // Bonus connectivité
             }
        }
        // Check Gap Négatif
        if (IS_VALID(mx, my) && g->board[my*BOARD_SIZE+mx] == EMPTY) {
             int mmx = mx - dx, mmy = my - dy;
             if (IS_VALID(mmx, mmy) && g->board[mmy*BOARD_SIZE+mmx] == player) {
                 score += 1000;
             }
        }
    }
    return score;
}

/*
    Fonction d'évaluation "Rayon X".
*/
int get_point_score(game *g, int x, int y, int player) {
    int total_score = 0;
    
    // Appel direct pour les 4 directions (Horizontal, Vertical, Diag1, Diag2)
    // Cela évite la boucle et les tableaux dx/dy
    total_score += evaluate_line(g, x, y, 1, 0, player);  // Horizontal
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    
    total_score += evaluate_line(g, x, y, 0, 1, player);  // Vertical
    if (total_score >= WIN_SCORE) return WIN_SCORE;

    total_score += evaluate_line(g, x, y, 1, 1, player);  // Diag Bas-Droite
    if (total_score >= WIN_SCORE) return WIN_SCORE;

    total_score += evaluate_line(g, x, y, 1, -1, player); // Diag Haut-Droite
    
    return total_score;
}

// Evaluate board reste le même, mais profitera de l'accélération
int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // Le score incrémental g->score est maintenu par apply_move
    // qui appelle get_point_score. Donc tout s'accélère.
    
    int score_diff = g->score[player] - g->score[opponent];
    
    // Facteur Captures (Très important pour votre variante)
    if (g->captures[player] >= 5) return WIN_SCORE;
    if (g->captures[opponent] >= 5) return -WIN_SCORE;
    
    score_diff += (g->captures[player] * 100000);
    score_diff -= (g->captures[opponent] * 100000);

    return score_diff;
}

/*
    Vérifie si un coup crée un "Free Three" (3 alignés avec espace pour devenir un 4 ouvert).
    Retourne 1 si c'est un Free Three, 0 sinon.
*/
int check_free_three_pattern(game *g, int x, int y, int dx, int dy, int player) {
    // On suppose que la pierre est déjà posée virtuellement en x,y
    int len = 1;
    
    // On cherche les extrémités de la chaîne continue
    int i = 1;
    while (IS_VALID(x + dx*i, y + dy*i) && g->board[GET_INDEX(x + dx*i, y + dy*i)] == player) { len++; i++; }
    int end1_x = x + dx*i, end1_y = y + dy*i; // Première case vide ou bloquante

    int j = 1;
    while (IS_VALID(x - dx*j, y - dy*j) && g->board[GET_INDEX(x - dx*j, y - dy*j)] == player) { len++; j++; }
    int end2_x = x - dx*j, end2_y = y - dy*j; // Autre extrémité

    // Un Free Three doit faire exactement 3 pierres
    if (len != 3) return 0;

    // Il doit être ouvert des deux côtés immédiatement (_XXX_)
    bool open1 = (IS_VALID(end1_x, end1_y) && g->board[GET_INDEX(end1_x, end1_y)] == EMPTY);
    bool open2 = (IS_VALID(end2_x, end2_y) && g->board[GET_INDEX(end2_x, end2_y)] == EMPTY);

    if (open1 && open2) {
        // Vérification supplémentaire : est-ce que ça peut devenir un Open 4 ?
        // Il faut qu'au moins un des côtés permette d'étendre encore après l'espace vide.
        // Ex: . _ X X X _ .
        // On regarde si on peut poser une pierre en end1 et que ce soit encore ouvert après
        // C'est une simplification, mais _XXX_ est la définition standard stricte.
        return 1;
    }
    return 0;
}

/*
    Vérifie la règle du Double-Three.
    Retourne TRUE si le coup est INTERDIT.
*/
bool is_double_three(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    g->board[idx] = player; // Pose temporaire
    
    int free_threes = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            free_threes++;
        }
    }

    g->board[idx] = EMPTY; // Annulation

    return (free_threes >= 2);
}

/* 
   NOUVELLE FONCTION : Affiche pourquoi un coup est un Double-Three.
   À appeler seulement quand on veut logger l'erreur.
*/
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
    
    if (free_threes >= 2) {
        printf("  => COUP INTERDIT (Total: %d)\n", free_threes);
    } else {
        printf("  => Coup Légal (Total: %d)\n", free_threes);
    }
    printf("---------------------------------------\n");

    g->board[idx] = EMPTY; 
}
#include "../include/gomoku.h"

/**
 * Verifie si les coordonnees sont dans les limites du plateau.
 */
bool in_bounds(int x, int y)
{
    return (x >= 0 && y >= 0 && x < BOARD_SIZE && y < BOARD_SIZE);
}

/**
 * Detecte les captures de paires autour d'un coup joue.
 * 
 * Pattern de capture : ALLY - ENEMY - ENEMY - ALLY
 * Scanne les 8 directions pour trouver les paires adverses encadrees.
 * 
 * Remplit removed_indices avec les indices 1D des pierres a capturer.
 * Retourne le nombre de pierres marquees pour capture (toujours pair).
 */
int get_captures_indices(game *gameData, int lx, int ly, int removed_indices[10])
{
    int owner = gameData->board[GET_INDEX(lx, ly)];
    int opponent = (owner == P1) ? P2 : P1;
    int count = 0;

    // Les 8 directions : (dx, dy)
    const int dirs[8][2] = {
        { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    };

    for (int d = 0; d < 8; d++)
    {
        int dx = dirs[d][0];
        int dy = dirs[d][1];

        int x1 = lx + dx,     y1 = ly + dy;
        int x2 = lx + 2 * dx, y2 = ly + 2 * dy;
        int x3 = lx + 3 * dx, y3 = ly + 3 * dy;

        // On vérifie que tout est dans le plateau
        if (!in_bounds(x3, y3)) // Si le point le plus loin est out, on arrête
            continue;

        // Conversion en 1D pour l'accès mémoire rapide
        int idx1 = GET_INDEX(x1, y1);
        int idx2 = GET_INDEX(x2, y2);
        int idx3 = GET_INDEX(x3, y3);

        /* Pattern: OWN - OPP - OPP - OWN */
        if (gameData->board[idx1] == opponent &&
            gameData->board[idx2] == opponent &&
            gameData->board[idx3] == owner)
        {
            // On stocke les indices 1D des pierres à supprimer
            removed_indices[count++] = idx1;
            removed_indices[count++] = idx2;
        }
    }
    return count;
}

/**
 * Compte les paires vulnerables apres avoir simule un coup.
 * 
 * Une paire est vulnerable si elle peut etre capturee par l'adversaire au coup suivant.
 * Utilise pour evaluer le risque d'un coup avant de le jouer.
 */
int count_vulnerable_pairs_after_move(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int opponent = (player == P1) ? P2 : P1;
    int vulnerable = 0;
    
    // Simuler le coup
    g->board[idx] = player;
    
    // 4 directions principales (on ne compte pas les diagonales en double)
    const int dirs[4][2] = {{1,0}, {0,1}, {1,1}, {1,-1}};
    
    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0], dy = dirs[d][1];
        int x = GET_X(idx), y = GET_Y(idx);

        // B2-FIX: Vérifier les patterns réellement vulnérables :
        // Une paire est IMMÉDIATEMENT vulnérable si un côté est déjà bloqué par l'adversaire
        // et l'autre côté est vide (l'adversaire peut jouer là au prochain coup).
        //
        // Pattern 1 : OPP - ME(idx) - ME(+1) - EMPTY
        //   (adversaire sur la gauche, vide sur la droite = vulnérable)
        {
            int xm = x - dx, ym = y - dy;   // côté gauche
            int xp1 = x + dx,  yp1 = y + dy;  // pierre alliée attendue
            int xp2 = x + 2*dx, yp2 = y + 2*dy; // côté droit
            if (IS_VALID(xm, ym) && IS_VALID(xp1, yp1) && IS_VALID(xp2, yp2)) {
                if (g->board[GET_INDEX(xm,  ym )] == opponent &&
                    g->board[GET_INDEX(xp1, yp1)] == player   &&
                    g->board[GET_INDEX(xp2, yp2)] == EMPTY) {
                    vulnerable++;
                }
            }
        }
        // Pattern 2 : EMPTY - ME(-1) - ME(idx) - OPP
        //   (vide sur la gauche, adversaire sur la droite = vulnérable)
        {
            int xm2 = x - 2*dx, ym2 = y - 2*dy;
            int xm1 = x - dx,   ym1 = y - dy;
            int xp1 = x + dx,   yp1 = y + dy;
            if (IS_VALID(xm2, ym2) && IS_VALID(xm1, ym1) && IS_VALID(xp1, yp1)) {
                if (g->board[GET_INDEX(xm2, ym2)] == EMPTY    &&
                    g->board[GET_INDEX(xm1, ym1)] == player   &&
                    g->board[GET_INDEX(xp1, yp1)] == opponent) {
                    vulnerable++;
                }
            }
        }
    }
    
    g->board[idx] = EMPTY;
    return vulnerable;
}

// Version optimisée pour l'IA : remplit la structure Undo et ne dessine rien
int apply_captures_for_ai(game *g, int lx, int ly, int player, int *captured_indices_buffer) {
    int total_removed = 0;
    int opponent = (player == P1) ? P2 : P1;
    
    const int dirs[8][2] = {
        { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    };

    for (int d = 0; d < 8; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];
        
        int x1 = lx + dx,     y1 = ly + dy;
        int x2 = lx + 2 * dx, y2 = ly + 2 * dy;
        int x3 = lx + 3 * dx, y3 = ly + 3 * dy;

        if (!in_bounds(x3, y3)) continue;

        int idx1 = GET_INDEX(x1, y1);
        int idx2 = GET_INDEX(x2, y2);
        int idx3 = GET_INDEX(x3, y3);

        if (g->board[idx1] == opponent &&
            g->board[idx2] == opponent &&
            g->board[idx3] == player) 
        {
            // Capture trouvée !
            g->board[idx1] = EMPTY;
            g->board[idx2] = EMPTY;
            
            // On enregistre pour le Undo
            captured_indices_buffer[total_removed++] = idx1;
            captured_indices_buffer[total_removed++] = idx2;
        }
    }
    return total_removed;
}

// Version LECTURE SEULE pour l'IA : compte les captures possibles sans modifier le plateau
int count_potential_captures(game *g, int lx, int ly, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int capture_count = 0;
    
    const int dirs[8][2] = {
        { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    };

    for (int d = 0; d < 8; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];
        
        int x1 = lx + dx,     y1 = ly + dy;
        int x2 = lx + 2 * dx, y2 = ly + 2 * dy;
        int x3 = lx + 3 * dx, y3 = ly + 3 * dy;

        if (!in_bounds(x3, y3)) continue;

        int idx1 = GET_INDEX(x1, y1);
        int idx2 = GET_INDEX(x2, y2);
        int idx3 = GET_INDEX(x3, y3);

        // Pattern: PLAYER - OPP - OPP - PLAYER
        if (g->board[idx1] == opponent &&
            g->board[idx2] == opponent &&
            g->board[idx3] == player) 
        {
            capture_count += 2; // 2 pierres capturées
        }
    }
    return capture_count;
}

int count_vulnerable_pairs(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int vulnerable_count = 0;
    
    const int dirs[4][2] = {
        {1, 0}, {0, 1}, {1, 1}, {1, -1}
    };
    
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            int idx = GET_INDEX(x, y);
            if (g->board[idx] != player) continue;
            
            for (int d = 0; d < 4; d++) {
                int dx = dirs[d][0];
                int dy = dirs[d][1];
                
                // Pattern: OPP - PLAYER - PLAYER - EMPTY (adversaire peut capturer)
                int x_m1 = x - dx, y_m1 = y - dy;
                int x_p1 = x + dx, y_p1 = y + dy;
                int x_p2 = x + 2*dx, y_p2 = y + 2*dy;
                
                if (IS_VALID(x_m1, y_m1) && IS_VALID(x_p1, y_p1) && IS_VALID(x_p2, y_p2)) {
                    int idx_m1 = GET_INDEX(x_m1, y_m1);
                    int idx_p1 = GET_INDEX(x_p1, y_p1);
                    int idx_p2 = GET_INDEX(x_p2, y_p2);
                    
                    // Pattern: OPP - ME - ME - EMPTY
                    if (g->board[idx_m1] == opponent &&
                        g->board[idx_p1] == player &&
                        g->board[idx_p2] == EMPTY) {
                        vulnerable_count++;
                    }
                }
                
                // Pattern inverse: EMPTY - PLAYER - PLAYER - OPP
                int x_m2 = x - 2*dx, y_m2 = y - 2*dy;
                
                if (IS_VALID(x_m2, y_m2) && IS_VALID(x_m1, y_m1) && IS_VALID(x_p1, y_p1)) {
                    int idx_m2 = GET_INDEX(x_m2, y_m2);
                    int idx_m1 = GET_INDEX(x_m1, y_m1);
                    int idx_p1 = GET_INDEX(x_p1, y_p1);
                    
                    if (g->board[idx_m2] == EMPTY &&
                        g->board[idx_m1] == player &&
                        g->board[idx_p1] == opponent) {
                        vulnerable_count++;
                    }
                }
            }
        }
    }
    
    return vulnerable_count / 2; // Chaque paire est comptée 2 fois (une fois par pierre)
}

/*
 * NOUVELLE FONCTION : Trouve la case qui permet de capturer une paire adverse
 * Retourne l'index de la case, ou -1 si pas de capture possible
 */
int find_capture_move(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), player);
        if (caps >= 2) { // Au moins une paire
            return i;
        }
    }
    return -1;
}
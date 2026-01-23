#include "../include/gomoku.h"

/*
 * Calcule si une case est une "jonction potentielle" pour un joueur.
 * Une jonction potentielle est une case qui appartient à 2+ lignes de formation.
 * Utilisée par evaluate_defensive_capture_value() dans ai_captures.c
 */
int compute_junction_potential(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY && g->board[idx] != player) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int formations_count = 0;
    
    for (int d = 0; d < 4; d++) {
        int stones_in_line = 0;
        int space_total = 0;
        
        /* Direction négative */
        for (int k = 1; k <= 4; k++) {
            int nx = x - dx[d] * k;
            int ny = y - dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            int cell = g->board[GET_INDEX(nx, ny)];
            if (cell == player) stones_in_line++;
            else if (cell == EMPTY) space_total++;
            else break;
        }
        
        /* Direction positive */
        for (int k = 1; k <= 4; k++) {
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            int cell = g->board[GET_INDEX(nx, ny)];
            if (cell == player) stones_in_line++;
            else if (cell == EMPTY) space_total++;
            else break;
        }
        
        /* Formation valide si potentiel >= 5 */
        if (stones_in_line >= 1 && stones_in_line + space_total + 1 >= 5) {
            formations_count++;
        }
    }
    
    return formations_count;
}
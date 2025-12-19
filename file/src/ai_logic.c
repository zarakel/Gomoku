#include "../include/gomoku.h"

void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;

    // Sauvegarde de l'état AVANT modification
    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];
    undo->captured_count = 0;

    // 1. Pose de la pierre
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player];

    // 2. Détection des captures (on stocke les indices AVANT de modifier)
    int opponent_val = opponent;
    const int dirs[8][2] = {
        { 1, 0}, { 0, 1}, {-1, 0}, { 0,-1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    };

    for (int d = 0; d < 8; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];
        
        int x1 = x + dx,     y1 = y + dy;
        int x2 = x + 2 * dx, y2 = y + 2 * dy;
        int x3 = x + 3 * dx, y3 = y + 3 * dy;

        if (!IS_VALID(x3, y3)) continue;

        int idx1 = GET_INDEX(x1, y1);
        int idx2 = GET_INDEX(x2, y2);
        int idx3 = GET_INDEX(x3, y3);

        // Pattern: PLAYER (vient d'être posé) - OPP - OPP - PLAYER
        if (g->board[idx1] == opponent_val &&
            g->board[idx2] == opponent_val &&
            g->board[idx3] == player) 
        {
            // Enregistrer pour undo
            undo->captured_indices[undo->captured_count++] = idx1;
            undo->captured_indices[undo->captured_count++] = idx2;
            
            // Appliquer la capture
            g->board[idx1] = EMPTY;
            g->board[idx2] = EMPTY;
            g->current_hash ^= zobrist_table[idx1][opponent_val];
            g->current_hash ^= zobrist_table[idx2][opponent_val];
        }
    }

    // 3. Mettre à jour le compteur de captures
    g->captures[player] += (undo->captured_count / 2);
}

void undo_move(game *g, int player, MoveUndo *undo) {
    int idx = undo->move_idx;
    int opponent = (player == P1) ? P2 : P1;

    // 1. Restaurer les pierres capturées
    for (int i = 0; i < undo->captured_count; i++) {
        int cap_idx = undo->captured_indices[i];
        g->board[cap_idx] = opponent;
        g->current_hash ^= zobrist_table[cap_idx][opponent];
    }

    // 2. Retirer notre pierre
    g->board[idx] = EMPTY;
    g->current_hash ^= zobrist_table[idx][player];
    
    // 3. Restaurer les compteurs de captures
    g->captures[P1] = undo->prev_captures[P1];
    g->captures[P2] = undo->prev_captures[P2];
}

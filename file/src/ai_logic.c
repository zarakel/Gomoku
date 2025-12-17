#include "../include/gomoku.h"

static void update_score_impact(game *g, int idx) {
    int x = GET_X(idx); int y = GET_Y(idx);
    g->score[P1] -= get_point_score(g, x, y, P1);
    g->score[P2] -= get_point_score(g, x, y, P2);
}

void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    int x = GET_X(idx); int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;

    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];

    update_score_impact(g, idx);
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player]; 
    g->score[P1] += get_point_score(g, x, y, P1);
    g->score[P2] += get_point_score(g, x, y, P2);

    undo->captured_count = apply_captures_for_ai(g, x, y, player, undo->captured_indices);
    g->captures[player] += (undo->captured_count / 2);

    if (undo->captured_count > 0) {
        for (int i = 0; i < undo->captured_count; i++) {
            int cap_idx = undo->captured_indices[i];
            int cx = GET_X(cap_idx); int cy = GET_Y(cap_idx);
            g->current_hash ^= zobrist_table[cap_idx][opponent];
            g->board[cap_idx] = opponent; 
            g->score[P1] -= get_point_score(g, cx, cy, P1);
            g->score[P2] -= get_point_score(g, cx, cy, P2);
            g->board[cap_idx] = EMPTY; 
            g->score[P1] += get_point_score(g, cx, cy, P1);
            g->score[P2] += get_point_score(g, cx, cy, P2);
        }
    }
}

void undo_move(game *g, int player, MoveUndo *undo) {
    int idx = undo->move_idx;
    int x = GET_X(idx); int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;

    if (undo->captured_count > 0) {
        for (int i = 0; i < undo->captured_count; i++) {
            int cap_idx = undo->captured_indices[i];
            int cx = GET_X(cap_idx); int cy = GET_Y(cap_idx);
            g->score[P1] -= get_point_score(g, cx, cy, P1);
            g->score[P2] -= get_point_score(g, cx, cy, P2);
            g->board[cap_idx] = opponent;
            g->current_hash ^= zobrist_table[cap_idx][opponent]; 
            g->score[P1] += get_point_score(g, cx, cy, P1);
            g->score[P2] += get_point_score(g, cx, cy, P2);
        }
    }

    g->score[P1] -= get_point_score(g, x, y, P1);
    g->score[P2] -= get_point_score(g, x, y, P2);
    g->board[idx] = EMPTY;
    g->current_hash ^= zobrist_table[idx][player]; 
    g->score[P1] += get_point_score(g, x, y, P1);
    g->score[P2] += get_point_score(g, x, y, P2);
    g->captures[P1] = undo->prev_captures[P1];
    g->captures[P2] = undo->prev_captures[P2];
}
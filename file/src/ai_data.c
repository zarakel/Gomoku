#include "../include/gomoku.h"

// Définition des globales
uint64_t zobrist_table[MAX_BOARD][3];
TTEntry transposition_table[TT_SIZE];
int killer_moves[MAX_DEPTH][2];
int history_heuristic[MAX_BOARD];
long long debug_node_count = 0;
long long debug_cutoff_count = 0;

static uint64_t rand64() {
    return (uint64_t)rand() ^ ((uint64_t)rand() << 15) ^ 
           ((uint64_t)rand() << 30) ^ ((uint64_t)rand() << 45) ^ 
           ((uint64_t)rand() << 60);
}

void init_zobrist() {
    for (int i = 0; i < MAX_BOARD; i++) {
        zobrist_table[i][0] = rand64();
        zobrist_table[i][P1] = rand64();
        zobrist_table[i][P2] = rand64();
    }
    memset(transposition_table, 0, sizeof(transposition_table));
}

void clear_heuristics() {
    memset(killer_moves, -1, sizeof(killer_moves));
    memset(history_heuristic, 0, sizeof(history_heuristic));
    debug_node_count = 0;
    debug_cutoff_count = 0;
}

void tt_save(uint64_t key, int depth, int val, int flag, int best_move) {
    int idx = key & (TT_SIZE - 1); 
    if (transposition_table[idx].key != key || depth >= transposition_table[idx].depth) {
        transposition_table[idx].key = key;
        transposition_table[idx].depth = depth;
        transposition_table[idx].value = val;
        transposition_table[idx].flag = flag;
        transposition_table[idx].best_move = best_move;
    }
}

TTEntry* tt_probe(uint64_t key) {
    int idx = key & (TT_SIZE - 1);
    if (transposition_table[idx].key == key) return &transposition_table[idx];
    return NULL;
}
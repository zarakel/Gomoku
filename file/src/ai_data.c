#include "../include/gomoku.h"

// Tables globales pour l'optimisation de la recherche
uint64_t zobrist_table[MAX_BOARD][3];        // Hash incremental des positions
TTEntry transposition_table[TT_SIZE];        // Cache des positions deja evaluees
int killer_moves[MAX_DEPTH][2];               // Coups qui causent souvent des cutoffs
int history_heuristic[MAX_BOARD];             // Historique de performance des coups
long long debug_node_count = 0;               // Compteur de noeuds explores
long long debug_cutoff_count = 0;             // Compteur de coupures alpha-beta
int ia_last_depth = 0;                        // Dernière profondeur complétée par l'IA

/**
 * Genere un nombre aleatoire 64-bit pour le hashing Zobrist.
 * Combine plusieurs appels a rand() pour couvrir les 64 bits.
 */
static uint64_t rand64() {
    return (uint64_t)rand() ^ ((uint64_t)rand() << 15) ^ 
           ((uint64_t)rand() << 30) ^ ((uint64_t)rand() << 45) ^ 
           ((uint64_t)rand() << 60);
}

/**
 * Initialise la table de hashing Zobrist avec des valeurs aleatoires.
 * Chaque case du plateau et chaque joueur ont une cle unique de 64 bits.
 * Permet de calculer le hash d'une position en temps O(1) par XOR incremental.
 */
void init_zobrist() {
    for (int i = 0; i < MAX_BOARD; i++) {
        zobrist_table[i][0] = rand64();
        zobrist_table[i][P1] = rand64();
        zobrist_table[i][P2] = rand64();
    }
    memset(transposition_table, 0, sizeof(transposition_table));
}

/**
 * Reinitialise les heuristiques de recherche entre les coups.
 * Efface killer moves, history heuristic et compteurs de debug.
 */
void clear_heuristics() {
    memset(killer_moves, -1, sizeof(killer_moves));
    memset(history_heuristic, 0, sizeof(history_heuristic));
    debug_node_count = 0;
    debug_cutoff_count = 0;
}

/**
 * Sauvegarde une position dans la table de transposition.
 * Remplace l'entree existante si la nouvelle profondeur est superieure ou egale.
 * Evite de recalculer des positions deja evaluees.
 */
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

/**
 * Recherche une position dans la table de transposition.
 * Retourne l'entree si trouvee, NULL sinon.
 * Complexite O(1) grace au hashing.
 */
TTEntry* tt_probe(uint64_t key) {
    int idx = key & (TT_SIZE - 1);
    if (transposition_table[idx].key == key) return &transposition_table[idx];
    return NULL;
}
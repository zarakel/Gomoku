#include "../include/gomoku.h"

//
// cand_refcount[n] = nombre de pierres (toute couleur) dans dist≤2 de n.
// Une case est candidate ssi board[n]==EMPTY && cand_refcount[n]>0.
// Les 5 fonctions ci-dessous sont O(25) — aucun scan du plateau.
// ---------------------------------------------------------------------------

static inline void cand_add(game *g, int idx) {
    if (g->in_cand[idx]) return;
    g->in_cand[idx] = true;
    g->cand_pos[idx] = g->cand_count;
    g->cand_list[g->cand_count++] = idx;
}

static inline void cand_remove(game *g, int idx) {
    if (!g->in_cand[idx]) return;
    g->in_cand[idx] = false;
    int pos = g->cand_pos[idx];
    int last = g->cand_list[--g->cand_count];
    g->cand_list[pos] = last;
    g->cand_pos[last] = pos;
}

// Après g->board[idx] = player  (pierre posée)
static void cand_on_place(game *g, int idx) {
    cand_remove(g, idx); // idx n'est plus une case vide
    int x = GET_X(idx), y = GET_Y(idx);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (!IS_VALID(nx, ny)) continue;
            int n = GET_INDEX(nx, ny);
            g->cand_refcount[n]++;
            if (g->board[n] == EMPTY && g->cand_refcount[n] == 1)
                cand_add(g, n);
        }
    }
}

// Après g->board[idx] = EMPTY  (pierre retirée)
static void cand_on_remove(game *g, int idx) {
    int x = GET_X(idx), y = GET_Y(idx);
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (!IS_VALID(nx, ny)) continue;
            int n = GET_INDEX(nx, ny);
            if (g->cand_refcount[n] > 0) g->cand_refcount[n]--;
            if (g->board[n] == EMPTY && g->cand_refcount[n] == 0)
                cand_remove(g, n);
        }
    }
    // idx vient de devenir vide; si des pierres l'entourent, c'est un candidat
    if (g->cand_refcount[idx] > 0) cand_add(g, idx);
}

// Reconstruit le candidate set depuis l'état courant du plateau.
// À appeler après tout reset ou chargement de position externe.
void cand_rebuild(game *g) {
    memset(g->cand_refcount, 0, sizeof(g->cand_refcount));
    memset(g->in_cand,       0, sizeof(g->in_cand));
    g->cand_count  = 0;
    g->stone_count = 0;
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] == EMPTY) continue;
        g->stone_count++;
        int x = GET_X(idx), y = GET_Y(idx);
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (!IS_VALID(nx, ny)) continue;
                g->cand_refcount[GET_INDEX(nx, ny)]++;
            }
        }
    }
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] == EMPTY && g->cand_refcount[idx] > 0)
            cand_add(g, idx);
    }
}

/**
 * Applique un coup sur le plateau avec evaluation incrementale.
 * 
 * Etapes :
 * 1. Sauvegarde l'etat pour undo
 * 2. Retire les anciens scores des lignes impactees
 * 3. Pose la pierre et met a jour le hash Zobrist
 * 4. Ajoute les nouveaux scores crees par la pierre
 * 5. Gere les captures et met a jour les scores autour
 * 
 * Complexite : O(1) grace a l'evaluation incrementale
 */
void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    
    // 1. Sauvegarde pour Undo
    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];

    // 2. RETIRER L'ANCIEN ÉTAT (Avant de poser la pierre)
    update_impacted_scores(g, x, y, true);

    // 3. POSER LA PIERRE
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player];
    cand_on_place(g, idx);
    g->stone_count++;

    // 4. CAPTURES
    int captured_indices[10];
    int nb_cap = apply_captures_for_ai(g, x, y, player, captured_indices);
    undo->captured_count = nb_cap;

    if (nb_cap > 0) {
        // Captures : les pierres retirées par apply_captures_for_ai étaient présentes
        // lors du step 2 (remove → old board). Pour corriger les scores :
        //   a) REMOVE les scores des lignes passant par chaque pierre capturée
        //      (dans l'état board post-pose mais pré-remove, c'est-à-dire captured stones
        //       sont déjà EMPTY car apply_captures_for_ai les a retirées).
        //      → On les remet temporairement pour retirer leur score correctement.
        //   b) On les re-retire du board.
        //   c) On ADD les scores de TOUTES les positions affectées (joué + captures)
        //      dans l'état final du board (toutes captures faites).
        //
        // Étape A : retirer les anciens scores autour des captures.
        // Les pierres capturées sont DÉJÀ retirées du board par apply_captures_for_ai.
        // On les remet TOUTES d'abord pour avoir le bon état "juste après la pose".
        g->captures[player] += (nb_cap / 2);
        for (int i = 0; i < nb_cap; i++) {
            undo->captured_indices[i] = captured_indices[i];
            g->board[captured_indices[i]] = opponent;  // remettre temporairement
        }
        // Maintenant board = stone posée + toutes captures encore présentes.
        // Retirer les scores de chaque capture.
        for (int i = 0; i < nb_cap; i++) {
            int cx = GET_X(captured_indices[i]);
            int cy = GET_Y(captured_indices[i]);
            update_impacted_scores(g, cx, cy, true);
        }
        // Étape B : retirer toutes les captures du board en même temps.
        for (int i = 0; i < nb_cap; i++) {
            g->board[captured_indices[i]] = EMPTY;
            cand_on_remove(g, captured_indices[i]);
            g->stone_count--;
            g->current_hash ^= zobrist_table[captured_indices[i]][opponent];
        }
        // Étape C : ajouter les nouveaux scores pour le coup joué ET les zones capturées.
        // Board = état final (stone posée, captures retirées).
        update_impacted_scores(g, x, y, false);
        for (int i = 0; i < nb_cap; i++) {
            int cx = GET_X(captured_indices[i]);
            int cy = GET_Y(captured_indices[i]);
            update_impacted_scores(g, cx, cy, false);
        }
    } else {
        // Pas de capture : l'update incrémental classique est correct.
        update_impacted_scores(g, x, y, false);
    }
}

/**
 * Annule un coup applique precedemment.
 * Restaure le plateau, les captures, et les scores incrementaux.
 * DOIT etre appele dans l'ordre inverse de apply_move pour garantir la coherence.
 */
void undo_move(game *g, int player, MoveUndo *undo) {
    int idx = undo->move_idx;
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    bool had_captures = (undo->captured_count > 0);

    if (had_captures) {
        // Symétrique de apply_move : inverser les 3 étapes (C, B, A).
        // État actuel : stone posée, captures retirées (état final d'apply_move).

        // Étape C inversée : retirer les scores du coup joué ET des zones capturées.
        update_impacted_scores(g, x, y, true);
        for (int i = 0; i < undo->captured_count; i++) {
            int cx = GET_X(undo->captured_indices[i]);
            int cy = GET_Y(undo->captured_indices[i]);
            update_impacted_scores(g, cx, cy, true);
        }

        // Étape B inversée : remettre toutes les captures sur le board.
        for (int i = undo->captured_count - 1; i >= 0; i--) {
            int c_idx = undo->captured_indices[i];
            g->board[c_idx] = opponent;
            g->current_hash ^= zobrist_table[c_idx][opponent];
            cand_on_place(g, c_idx);
            g->stone_count++;
        }

        // Étape A inversée : re-scorer les lignes autour des captures (maintenant rétablies).
        for (int i = 0; i < undo->captured_count; i++) {
            int cx = GET_X(undo->captured_indices[i]);
            int cy = GET_Y(undo->captured_indices[i]);
            update_impacted_scores(g, cx, cy, false);
        }

        // Restaurer les compteurs de captures.
        g->captures[player] = undo->prev_captures[player];
        g->captures[opponent] = undo->prev_captures[opponent];

        // Retirer la pierre jouée.
        // Score déjà retiré dans l'étape C inversée ci-dessus.
        g->board[idx] = EMPTY;
        g->current_hash ^= zobrist_table[idx][player];
        cand_on_remove(g, idx);
        g->stone_count--;

        // Ajouter les scores du vide restauré.
        update_impacted_scores(g, x, y, false);
    } else {
        g->captures[player] = undo->prev_captures[player];
        g->captures[opponent] = undo->prev_captures[opponent];

        // Retirer la pierre jouée.
        update_impacted_scores(g, x, y, true);
        g->board[idx] = EMPTY;
        g->current_hash ^= zobrist_table[idx][player];
        cand_on_remove(g, idx);
        g->stone_count--;
        update_impacted_scores(g, x, y, false);
    }
}

/**
 * Met a jour les compteurs de menaces lorsqu'une ligne change de valeur.
 * Retire l'ancien niveau de menace et ajoute le nouveau.
 * Recalcule le niveau de menace maximum du joueur.
 */
void update_line_stats(game *g, int player, int old_score, int new_score) {
    // 1. Retirer l'ancienne menace
    if (old_score != 0) { // Si c'était 0 (vide), rien à retirer
        int old_lvl = get_threat_level(old_score);
        g->threat_counts[player][old_lvl]--;
    }
    
    // 2. Ajouter la nouvelle menace
    if (new_score != 0) {
        int new_lvl = get_threat_level(new_score);
        g->threat_counts[player][new_lvl]++;
    }
    
    // 3. Mettre à jour le Max Threat (Lazy update)
    // On cherche le plus haut niveau qui a un compteur > 0
    for (int lvl = IDX_WIN; lvl >= 0; lvl--) {
        if (g->threat_counts[player][lvl] > 0) {
            g->max_threat_level[player] = lvl; // ou stocker le score type correspondant
            return;
        }
    }
    g->max_threat_level[player] = 0;
}
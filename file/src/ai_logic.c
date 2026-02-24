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
    
    // 1. Sauvegarde pour Undo
    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];

    // 2. RETIRER L'ANCIEN ÉTAT (Avant de poser la pierre)
    // On retire les scores des lignes qui passent par (x,y) car elles vont être modifiées/coupées
    update_impacted_scores(g, x, y, true); // true = remove

    // 3. POSER LA PIERRE
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player];
    cand_on_place(g, idx);   // O(25) : mise à jour candidats
    g->stone_count++;

    // 4. AJOUTER LE NOUVEL ÉTAT (Positionnel pierre posée)
    // On ajoute les nouveaux scores créés par cette pierre
    update_impacted_scores(g, x, y, false); // false = add

    // GESTION DES CAPTURES
    // Detecte et applique les captures de paires adverses
    // Une capture = 2 pierres adjacentes entourees par nos pierres
    int captured_indices[10];
    int nb_cap = apply_captures_for_ai(g, x, y, player, captured_indices);
    
    undo->captured_count = nb_cap;
    
    // Mise a jour du compteur de paires capturees
    // nb_cap est en pierres individuelles, on divise par 2 pour obtenir les paires
    if (nb_cap > 0) {
        g->captures[player] += (nb_cap / 2);
    }
    // ---------------------------

    for(int i=0; i<nb_cap; i++) {
        undo->captured_indices[i] = captured_indices[i];
        int cx = GET_X(captured_indices[i]);
        int cy = GET_Y(captured_indices[i]);
        
        // La pierre capturée a été retirée par apply_captures_for_ai
        // Mais nous devons mettre à jour le score incrémental autour d'elle !
        
        // Astuce : La pierre est DÉJÀ partie du plateau (EMPTY).
        // Donc update_impacted_scores va voir du vide et calculer le score "post-capture".
        // Il nous manque l'étape "retirer le score de la pierre adverse qui était là".
        
        // On remet temporairement la pierre adverse pour retirer son score proprement
        int opponent = (player == P1) ? P2 : P1;
        g->board[captured_indices[i]] = opponent;
        
        update_impacted_scores(g, cx, cy, true); // Retirer score adverse (mort)
        
        g->board[captured_indices[i]] = EMPTY;   // On l'enlève pour de bon
        
        update_impacted_scores(g, cx, cy, false); // Ajouter score du vide (nouvelles ouvertures potentielles)
        cand_on_remove(g, captured_indices[i]);   // O(25) : la case redevient candidate
        g->stone_count--;
        
        // Hash update (Capture)
        g->current_hash ^= zobrist_table[captured_indices[i]][opponent];
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

    // 1. ANNULER LES CAPTURES (D'abord, pour restaurer l'environnement)
    if (undo->captured_count > 0) {
         for(int i=0; i<undo->captured_count; i++) {
            int c_idx = undo->captured_indices[i];
            int cx = GET_X(c_idx);
            int cy = GET_Y(c_idx);

            // On retire le score actuel (vide)
            update_impacted_scores(g, cx, cy, true);

            // On remet la pierre adverse
            g->board[c_idx] = opponent;
            g->current_hash ^= zobrist_table[c_idx][opponent];
            cand_on_place(g, c_idx);   // O(25)
            g->stone_count++;

            // On rajoute son score restauré
            update_impacted_scores(g, cx, cy, false);
        }
    }
    g->captures[player] = undo->prev_captures[player];
    g->captures[opponent] = undo->prev_captures[opponent];

    // 2. RETIRER LA PIERRE JOUÉE
    // On retire son score actuel
    update_impacted_scores(g, x, y, true);

    g->board[idx] = EMPTY;
    g->current_hash ^= zobrist_table[idx][player];
    cand_on_remove(g, idx);   // O(25)
    g->stone_count--;

    // 3. RESTAURER L'ÉTAT DU VIDE
    // On remet les scores tels qu'ils étaient quand c'était vide
    update_impacted_scores(g, x, y, false);
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
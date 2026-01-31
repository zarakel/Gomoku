#include "../include/gomoku.h"

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

    // 4. AJOUTER LE NOUVEL ÉTAT (Positionnel pierre posée)
    // On ajoute les nouveaux scores créés par cette pierre
    update_impacted_scores(g, x, y, false); // false = add

    // 5. GESTION DES CAPTURES
    int captured_indices[10];
    int nb_cap = apply_captures_for_ai(g, x, y, player, captured_indices);
    
    undo->captured_count = nb_cap;
    
    // --- AJOUT CORRECTIF ICI ---
    // On met à jour le compteur de paires capturées (nb_cap est en pierres, donc / 2)
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
        
        // Hash update (Capture)
        g->current_hash ^= zobrist_table[captured_indices[i]][opponent];
    }
}

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

    // 3. RESTAURER L'ÉTAT DU VIDE
    // On remet les scores tels qu'ils étaient quand c'était vide
    update_impacted_scores(g, x, y, false);
}

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
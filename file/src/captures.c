#include "../include/gomoku.h"

/* helper: checks if coordinates are on board */
bool in_bounds(int x, int y)
{
    return (x >= 0 && y >= 0 && x < BOARD_SIZE && y < BOARD_SIZE);
}

/* helper: logic to find captures. 
   Fills 'removed_indices' with the 1D index of stones to remove.
   Returns the number of stones marked for removal. */
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

/* Main function: Validates, Updates Board, Updates Score */
void checkPieceCapture(game *gameData, screen *windows, int lx, int ly)
{
    // 1. Validation basique
    if (!gameData || !in_bounds(lx, ly)) return;
    
    // Vérifier que la case n'est pas vide (le coup vient d'être joué)
    int current_idx = GET_INDEX(lx, ly);
    int owner = gameData->board[current_idx];
    if (owner != P1 && owner != P2) return;

    // 2. Calcul des captures (Logique pure, très rapide)
    int removed_indices[10]; // Max 8 pierres capturables théoriquement
    int count = get_captures_indices(gameData, lx, ly, removed_indices);

    // 3. Application des changements (Si captures il y a)
    if (count > 0)
    {
        for (int i = 0; i < count; i++)
        {
            int idx = removed_indices[i];
            
            // Mise à jour Mémoire
            gameData->board[idx] = EMPTY;
            
            // Mise à jour Graphique (Seulement si windows existe, pour compatibilité IA future)
            // if (windows) {
                drawSquare(windows, GET_X(idx), GET_Y(idx), EMPTY);
            // }
        }

        // Mise à jour Scores (Chaque paire vaut 1 point de capture)
        // count est le nombre de pierres, donc count / 2 est le nombre de paires
        gameData->captures[owner] += (count / 2);
        
        // On notifie que l'écran a changé
        windows->changed = true;
    }
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
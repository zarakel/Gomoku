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

/*
 * NOUVELLE FONCTION : Compte les paires du joueur qui sont VULNÉRABLES à une capture
 * Pattern vulnérable : . X X _ où _ est vide et . est l'adversaire qui peut capturer
 * Retourne le nombre de paires vulnérables
 */
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

/*
 * NOUVELLE FONCTION : Trouve la case qui BLOQUE une capture imminente de nos paires
 * L'adversaire pourrait capturer si on ne bloque pas
 */
int find_capture_block_move(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // Pour chaque case vide, vérifier si l'adversaire y jouerait pour capturer
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // Simuler le coup adverse
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (caps >= 2) { // L'adversaire capturerait au moins une paire
            // Cette case est critique ! On doit soit la bloquer, soit protéger nos paires
            return i;
        }
    }
    return -1;
}

/*
 * NOUVELLE FONCTION : Trouve une case qui protège nos paires vulnérables
 * Une paire est vulnérable si l'adversaire peut la capturer au prochain coup
 * 
 * Stratégies de protection :
 * 1. Bloquer la case où l'adversaire capturerait
 * 2. Ajouter une pierre pour créer un pattern non-capturable (3 pierres au lieu de 2)
 * 3. Capturer nous-mêmes pour réduire la pression
 */
int find_vulnerable_pair_protection(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    const int dirs[4][2] = {
        {1, 0}, {0, 1}, {1, 1}, {1, -1}
    };
    
    int best_protection = -1;
    int best_score = -1;
    
    /* Scanner toutes nos pierres pour trouver les paires vulnérables */
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int dx = dirs[d][0];
            int dy = dirs[d][1];
            
            /* Pattern vulnérable : . OPP ME ME OPP_CAN_PLAY */
            /* Ou : OPP_CAN_PLAY ME ME OPP . */
            
            /* Vérifier si c'est le début d'une paire */
            int x_next = x + dx;
            int y_next = y + dy;
            if (!IS_VALID(x_next, y_next)) continue;
            if (g->board[GET_INDEX(x_next, y_next)] != player) continue;
            
            /* On a une paire à (x,y) et (x_next, y_next) */
            
            /* Vérifier vulnérabilité côté "avant" */
            int x_before = x - dx;
            int y_before = y - dy;
            int x_after = x_next + dx;
            int y_after = y_next + dy;
            
            /* Pattern : EMPTY - ME - ME - OPPONENT (capturable) */
            if (IS_VALID(x_before, y_before) && IS_VALID(x_after, y_after)) {
                int idx_before = GET_INDEX(x_before, y_before);
                int idx_after = GET_INDEX(x_after, y_after);
                
                if (g->board[idx_before] == EMPTY && g->board[idx_after] == opponent) {
                    /* Vulnérable ! L'adversaire peut jouer en idx_before */
                    /* Protection : jouer nous-mêmes en idx_before */
                    int protection_score = evaluate_move_with_captures_full(g, idx_before, player);
                    if (protection_score > best_score || best_protection == -1) {
                        best_score = protection_score;
                        best_protection = idx_before;
                    }
                }
                
                /* Pattern inverse : OPPONENT - ME - ME - EMPTY */
                if (g->board[idx_before] == opponent && g->board[idx_after] == EMPTY) {
                    int protection_score = evaluate_move_with_captures_full(g, idx_after, player);
                    if (protection_score > best_score || best_protection == -1) {
                        best_score = protection_score;
                        best_protection = idx_after;
                    }
                }
            }
            
            /* Aussi vérifier 2 cases avant/après (pattern étendu) */
            int x_before2 = x - 2*dx;
            int y_before2 = y - 2*dy;
            int x_after2 = x_next + 2*dx;
            int y_after2 = y_next + 2*dy;
            
            /* Pattern : OPP - EMPTY - ME - ME - ? */
            if (IS_VALID(x_before, y_before) && IS_VALID(x_before2, y_before2)) {
                int idx_before = GET_INDEX(x_before, y_before);
                int idx_before2 = GET_INDEX(x_before2, y_before2);
                
                if (g->board[idx_before] == EMPTY && g->board[idx_before2] == opponent) {
                    /* Vulnérable via pattern OPP - empty - ME - ME */
                    /* Protection : jouer en idx_before pour casser le pattern */
                    int protection_score = evaluate_move_with_captures_full(g, idx_before, player);
                    protection_score += 50000; /* Bonus car c'est une protection */
                    if (protection_score > best_score || best_protection == -1) {
                        best_score = protection_score;
                        best_protection = idx_before;
                    }
                }
            }
        }
    }
    
    return best_protection;
}
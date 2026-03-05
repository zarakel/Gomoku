# include "../include/gomoku.h"

bool isIaTurn(int iaTurn, int turn){return (iaTurn == turn);}

void resetGame(game *gameData, screen *windows)
{
    #ifdef DEBUG
        printf("--- GAME RESET ---\n");
    #endif
    // Reset du plateau (inchangé)
    memset(gameData->board, EMPTY, sizeof(gameData->board));
    
    gameData->captures[P1] = 0;
    gameData->captures[P2] = 0;
    gameData->turn = P1;
    gameData->game_over = false;
    gameData->winner = 0;
    
    memset(gameData->pos_score, 0, sizeof(gameData->pos_score));
    memset(gameData->threat_counts, 0, sizeof(gameData->threat_counts));
    memset(gameData->max_threat_level, 0, sizeof(gameData->max_threat_level));
    
    // ===== NOUVEAU : Reset Crisis State =====
    gameData->in_crisis = false;
    gameData->crisis_immediate_win = false;
    gameData->crisis_level = 0;
    gameData->crisis_move_count = 0;
    memset(gameData->crisis_moves, -1, sizeof(gameData->crisis_moves));
    // =========================================

    memset(transposition_table, 0, sizeof(transposition_table));
    gameData->current_hash = 0; // Reset hash Zobrist : la nouvelle partie repart d'un état propre.

    resetTimer(&gameData->ia_timer);
    clear_heuristics();
    cand_rebuild(gameData); // Remet à zéro le candidate set incrémental
    
    windows->changed = true;
    windows->resized = true;
}
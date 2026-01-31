#include "../include/gomoku.h"

// Helper : Vérifie l'alignement de 5 pions pour un joueur donné à partir d'un point
static bool checkFiveInARow(int *board, int idx, int player)
{
    int y = GET_Y(idx);
    
    // Axes: Horizontal, Vertical, Diag \, Diag /
    int axes[4] = {1, BOARD_SIZE, BOARD_SIZE + 1, BOARD_SIZE - 1};

    for (int a = 0; a < 4; a++)
    {
        int offset = axes[a];
        int count = 1;

        // Vers l'avant
        for (int i = 1; i < 5; i++) {
            int next = idx + (i * offset);
            int nx = GET_X(next); 
            int ny = GET_Y(next);
            
            // Vérif bords et couleur
            if (!IS_VALID(nx, ny) || board[next] != player) break;
            
            // Vérif anti-wrap (saut de ligne horizontal)
            if (a == 0 && ny != y) break; 
            
            count++;
        }

        // Vers l'arrière
        for (int i = 1; i < 5; i++) {
            int prev = idx - (i * offset);
            int px = GET_X(prev);
            int py = GET_Y(prev);

            if (!IS_VALID(px, py) || board[prev] != player) break;
            if (a == 0 && py != y) break;

            count++;
        }

        if (count >= 5) return true; // Victoire alignement
    }
    return false;
}
void checkVictoryCondition(game *gameData)
{
    if (gameData->game_over) return;

    // 1. Victoire par CAPTURES
    if (gameData->captures[P1] >= 5) {
        gameData->winner = P1; // Stockage
        gameData->game_over = true;
        printf("VICTOIRE P1 (Noir) par captures !\n");
        return;
    }
    if (gameData->captures[P2] >= 5) {
        gameData->winner = P2; // Stockage
        gameData->game_over = true;
        printf("VICTOIRE P2 (Blanc) par captures !\n");
        return;
    }

    // 2. Victoire par ALIGNEMENT
    for (int i = 0; i < MAX_BOARD; i++)
    {
        if (gameData->board[i] != EMPTY)
        {
            if (checkFiveInARow(gameData->board, i, gameData->board[i]))
            {
                gameData->winner = gameData->board[i]; // Stockage du VRAI gagnant
                gameData->game_over = true;
                printf("VICTOIRE JOUEUR %d par alignement !\n", gameData->winner);
                return;
            }
        }
    }
}
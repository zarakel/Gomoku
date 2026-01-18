#include "../include/gomoku.h"

void printInformation(screen *windows, game *gameData)
{
    // 1. Nettoyage de l'ancien texte pour éviter les fuites de mémoire et superpositions
    if (windows->text_img)
    {
        mlx_delete_image(windows->mlx, windows->text_img);
        windows->text_img = NULL;
    }

    // 2. Préparation du buffer de texte
    char info_buffer[256];
    char turn_str[50];
    
    if (gameData->game_over)
    {
        int winner = (gameData->turn == P1) ? P2 : P1; // Le tour a changé après le coup gagnant
        snprintf(turn_str, sizeof(turn_str), "GAME OVER - WINNER: P%d", winner);
    }
    else
    {
        snprintf(turn_str, sizeof(turn_str), "Turn: %s", (gameData->turn == P1) ? "P1 (Black)" : "P2 (White)");
    }

    // Formatage des infos : Tour | Captures P1 | Captures P2 | Temps IA
    snprintf(info_buffer, sizeof(info_buffer), 
        "%s | Captures P1: %d | Captures P2: %d | IA Time: %.3fs", 
        turn_str,
        gameData->captures[P1], 
        gameData->captures[P2], 
        gameData->ia_timer.elapsed
    );

    // 3. Création de la nouvelle image de texte
    // (x: 10, y: 5) pour l'afficher en haut à gauche
    
    // AJOUT : Affichage du label sur le bouton (Position ajustée à vue d'oeil)
    // On doit utiliser une autre image ou réutiliser le put_string, 
    // mais MLX42 gère chaque string comme une image. 
    // Pour faire simple, on ajoute un 2ème put_string.
    // Note: Pour faire propre, il faudrait stocker ce pointeur aussi pour le delete,
    // mais ici on va simplifier.
    windows->text_img = mlx_put_string(windows->mlx, info_buffer, 10, 5);
//    mlx_put_string(windows->mlx, "RESTART", BTN_X + 10, BTN_Y + 5);

}
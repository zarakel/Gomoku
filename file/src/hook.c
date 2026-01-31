#include "../include/gomoku.h"

void resize(int32_t width, int32_t height, void *param)
{
    screen *windows = (struct screen *)param;

    windows->width = width;
    windows->height = height;

    if (windows->img) mlx_delete_image(windows->mlx, windows->img);
    
    windows->img = mlx_new_image(windows->mlx, width, height);
    // On met l'image de fond en couche 0
    mlx_image_to_window(windows->mlx, windows->img, 0, 0);

    // ASTUCE PROPRE :
    // Le texte "RESTART" existe déjà (windows->restart_text).
    // Mais comme on vient de remettre l'image de fond (img), elle risque de passer DEVANT le texte.
    // On supprime l'instance d'affichage du texte et on la remet pour qu'elle soit au-dessus.
    
    if (windows->restart_text)
    {
        // On détruit l'ancienne image de texte pour la recréer proprement par-dessus
        // Ou plus simple : on supprime et on rappelle initGUI
        mlx_delete_image(windows->mlx, windows->restart_text);
        windows->restart_text = NULL;
        initGUI(windows); 
    }

    windows->resized = true;
    windows->changed = true;
}

void cursor(double xpos, double ypos, void *param)
{
    screen *windows = (struct screen *)param;
    windows->x = xpos;
    windows->y = ypos;
}

void keyhook(mlx_key_data_t keydata, void *param)
{
    both *data = (both *)param; // Assure-toi que le cast correspond à ta structure
    screen *windows = data->windows;
    game *gameData = data->gameData;

    if (keydata.key == MLX_KEY_ESCAPE && keydata.action == MLX_PRESS)
    {
        #ifdef DEBUG
            printf("Escape key pressed, closing window.\n");
        #endif
        mlx_close_window(windows->mlx);
    }
    // Activation/Désactivation IA
    if (keydata.key == MLX_KEY_SPACE && keydata.action == MLX_PRESS)
    {
        // Si 0 -> Devient P2 (IA joue les Blancs/O)
        // Si non 0 -> Devient 0 (Humain vs Humain)
        gameData->iaTurn = (gameData->iaTurn == 0) ? P2 : 0;
        #ifdef DEBUG
            printf("IA Mode: %s\n", gameData->iaTurn ? "ON (Player 2)" : "OFF");
        #endif
    }

    // Ajout de la touche H pour le Hint
    if (keydata.key == MLX_KEY_H && keydata.action == MLX_PRESS)
    {
        printf("🔍 Touche H pressée !\n"); // <--- DEBUG
        if (!data->gameData->game_over)
            suggest_move(data->gameData, data->windows, data->gameData->turn);
        else
            printf("⚠️ Game Over, pas de hint.\n"); // <--- DEBUG
    }
}

void mousehook(mouse_key_t button, action_t action, modifier_key_t mods, void *param)
{
    both *args = (struct both *)param;
    screen *windows = args->windows;
    game *gameData = args->gameData;
    (void)mods;

    // GESTION DU BOUTON RESET (Prioritaire sur le reste)
    if (button == MLX_MOUSE_BUTTON_LEFT && action == MLX_PRESS)
    {
        // On vérifie si la souris est dans le rectangle du bouton
        if (windows->x >= BTN_X && windows->x <= BTN_X + BTN_W &&
            windows->y >= BTN_Y && windows->y <= BTN_Y + BTN_H)
        {
            resetGame(gameData, windows);
            return; // On arrête là, on ne pose pas de pion !
        }
    }

    // Pas de clic si c'est au tour de l'IA !
    if (isIaTurn(gameData->iaTurn, gameData->turn))
        return;

    // Marges définies dans .h (ou ici si locales)
    int ml = 30, mr = 30, mt = 30, mb = 30; 

    int drawable_w = (int)windows->width - ml - mr;
    int drawable_h = (int)windows->height - mt - mb;

    if (drawable_w <= 0 || drawable_h <= 0) return;

    /* ignore clicks outside the board rectangle */
    if (windows->x < ml || windows->x >= (ml + drawable_w) ||
        windows->y < mt || windows->y >= (mt + drawable_h))
         return;

    double rel_x = windows->x - ml;
    double rel_y = windows->y - mt;

    // Conversion précise souris -> case
    int cell_x = (int)(rel_x * windows->board_size / (double)drawable_w);
    int cell_y = (int)(rel_y * windows->board_size / (double)drawable_h);

    // Clamp (Sécurité)
    if (cell_x < 0) cell_x = 0;
    if (cell_y < 0) cell_y = 0;
    if (cell_x >= windows->board_size) cell_x = windows->board_size - 1;
    if (cell_y >= windows->board_size) cell_y = windows->board_size - 1;

    if (button == MLX_MOUSE_BUTTON_LEFT && action != MLX_RELEASE)
    {
        if (!gameData->game_over)
        {
            // Vérification case vide via Index 1D
            int idx = GET_INDEX(cell_x, cell_y);
            
            if (gameData->board[idx] == EMPTY)
            {
                // --- VÉRIFICATION RÈGLE DOUBLE-THREE (HUMAIN) ---
                if (is_double_three(gameData, idx, gameData->turn)) {
                    // On vérifie si c'est une capture (Exception à la règle)
                    int capture_indices[10];
                    int caps = apply_captures_for_ai(gameData, cell_x, cell_y, gameData->turn, capture_indices);
                    
                    // On annule la simulation de capture faite par apply_captures_for_ai (car on ne joue pas encore)
                    int opponent = (gameData->turn == P1) ? P2 : P1;
                    for(int k=0; k<caps; k++) gameData->board[capture_indices[k]] = opponent;

                    if (caps == 0) {
                        printf("COUP INTERDIT : Double-Three !\n");
                        explain_double_three(gameData, idx, gameData->turn); // Pour le debug
                        return; // On sort, le coup n'est pas joué
                    }
                }
                // ----------------------------------------

                // 1. Jouer le coup
                gameData->board[idx] = gameData->turn;
                
                // 2. Dessiner
                drawSquare(windows, cell_x, cell_y, gameData->turn);
                
                // 3. Gérer les captures
                checkPieceCapture(gameData, windows, cell_x, cell_y);
                
                // --- AJOUT : VÉRIFICATION VICTOIRE IMMÉDIATE ---
                checkVictoryCondition(gameData);
                if (gameData->game_over) {
                    windows->changed = true; // Pour afficher le message de fin
                    return; // ON ARRÊTE TOUT ICI, on ne change pas de tour !
                }
                // -----------------------------------------------
                
                // 4. Changer de tour
                gameData->turn = (gameData->turn == P1) ? P2 : P1;
                gameData->hint_idx = -1; // Effacer le hint après coup joué
                windows->changed = true;
            }
        }
    }
}
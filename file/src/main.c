#include "../include/gomoku.h"

/**
 * Valide les arguments de ligne de commande.
 * Pour l'instant, accepte uniquement le mode sans arguments.
 */
bool checkArgs(int argc, char **argv, void**args)
{
    if (argc == 1)
        *args = NULL;
    else
    {
        (void)argv;
        *args = NULL;
    }
    return true;
}

/**
 * Initialise toutes les structures de donnees du jeu.
 * Configure l'ecran, le plateau, les compteurs, et les timers.
 * Doit etre appele avant le debut de la partie.
 */
bool initialized(void *args, screen *windows, game *gameData)
{
    (void) args;

    // Screen Init
    windows->width = WIDTH;
    windows->height = HEIGHT;
    windows->moved = false;
    windows->resized = true;
    windows->isClicked = false;
    windows->changed = true;
    windows->board_size = BOARD_SIZE;
    windows->text_img = NULL;
    windows->restart_text = NULL;

    // Game Init
    gameData->board_size = BOARD_SIZE;

    // OPTIMISATION: memset met tout le tableau à 0 en une seule instruction CPU
    memset(gameData->board, EMPTY, sizeof(gameData->board));
    
    // Init captures
    gameData->captures[P1] = 0;
    gameData->captures[P2] = 0;

    gameData->iaTurn = 2; // 0 = Pas d'IA, sinon 1 ou 2
    gameData->turn = P1;  // P1 commence toujours
    gameData->game_over = false;
    gameData->hint_idx = -1;

    // Timer Init
    gameData->ia_timer.elapsed = 0.0;
    gameData->ia_timer.running = false;
    gameData->ia_timer.start_ts.tv_sec = 0;
    gameData->ia_timer.start_ts.tv_nsec = 0;

    gameData->winner = 0;

    gameData->pos_score[0] = 0;
    gameData->pos_score[P1] = 0;
    gameData->pos_score[P2] = 0;

    memset(gameData->pos_score, 0, sizeof(gameData->pos_score));
    memset(gameData->threat_counts, 0, sizeof(gameData->threat_counts));
    memset(gameData->max_threat_level, 0, sizeof(gameData->max_threat_level));

    gameData->current_hash = 0;

    gameData->in_crisis = false;
    gameData->crisis_level = 0;
    gameData->crisis_move_count = 0;
    memset(gameData->crisis_moves, -1, sizeof(gameData->crisis_moves));

    cand_rebuild(gameData); // Initialise le candidate set incrémental

    return true;
}

/**
 * Affiche toutes les pieces presentes sur le plateau.
 * Parcourt le tableau 1D et dessine chaque pierre visible.
 */
void putPiecesOnBoard(screen *windows, int *board)
{
    // On parcourt le tableau 1D comme une matrice pour l'affichage
    for (int y = 0; y < windows->board_size; y++)
    {
        for (int x = 0; x < windows->board_size; x++)
        {
            // Utilisation de la Macro pour récupérer la valeur
            int val = board[GET_INDEX(x, y)];
            
            if (val == P1 || val == P2)
                drawSquare(windows, x, y, val);
            else if (val == PREVIS)
                 drawSquare(windows, x, y, val); // Si tu as une couleur de prévis
        }
    }
}

void resetScreen(screen *windows, game *gameData)
{
    printBlack(windows);
    putCadrillage(windows);
    putPiecesOnBoard(windows, gameData->board);

    // ✅ On rajoute l'affichage du HINT s'il existe (car il n'est pas dans board[])
    if (gameData->hint_idx != -1)
    {
        drawSquare(windows, GET_X(gameData->hint_idx), GET_Y(gameData->hint_idx), HINT);
    }
}

/**
 * Boucle principale du jeu, appelee a chaque frame.
 * Gere les mises a jour d'affichage, les tours de l'IA, et les conditions de victoire.
 */
void gameLoop(void *param)
{
    both        *args = (struct both *)param;
    screen      *windows = args->windows;
    game        *gameData = args->gameData;

    // Process websocket events (non-blocking)
    if (args->mgr)
        mg_mgr_poll(args->mgr, 0);

    if (windows->changed)
    {
        resetScreen(windows, gameData);
        if (windows->resized)
        {
            windows->resized = false;
        }

        // Gestion du tour IA
        if (isIaTurn(gameData->iaTurn, gameData->turn) && !gameData->game_over)
        {
            makeIaMove(gameData, windows);
            
            // Après le coup de l'IA, on change de tour et on redraw
            checkVictoryCondition(gameData);
            gameData->turn = (gameData->turn == P1) ? P2 : P1;
            windows->changed = true; // Forcer le redraw après coup IA

            // ✅ Broadcast IA move to frontend
            if (args->mgr)
                broadcast_board_state_external(args->mgr, gameData, windows);
        }
        else
        {
            resetTimer(&gameData->ia_timer);
        }

        checkVictoryCondition(gameData);
        // Le changement de tour Humain se fait généralement dans le mousehook
        windows->changed = false;
    }

    printInformation(windows, gameData);
}

/**
 * Initialise la fenetre graphique MLX et lance la boucle de jeu.
 * Configure tous les hooks (souris, clavier, resize) avant de demarrer.
 */
void launchGame(game *gameData, screen *windows)
{
    both args;
    args.windows = windows;
    args.gameData = gameData;
    args.mgr = NULL;

    init_websocket(&args);

    // Init MLX
    windows->mlx = mlx_init((int32_t)windows->width, (int32_t)windows->height, "Gomoku IA", true);
    if (!windows->mlx)
    {
        perror("mlx_init failed");
        cleanup_websocket(&args);
        exit(EXIT_FAILURE);
    }

    windows->img = mlx_new_image(windows->mlx, windows->width, windows->height);
    if (mlx_image_to_window(windows->mlx, windows->img, 0, 0) == -1)
    {
        mlx_close_window(windows->mlx);
        cleanup_websocket(&args);
        puts(mlx_strerror(mlx_errno));
        exit(EXIT_FAILURE);
    }

    initGUI(windows);

    // Hooks
    mlx_resize_hook(windows->mlx, &resize, windows);
    mlx_loop_hook(windows->mlx, &gameLoop, &args);
    mlx_cursor_hook(windows->mlx, &cursor, windows);
    mlx_mouse_hook(windows->mlx, &mousehook, &args);
    mlx_key_hook(windows->mlx, &keyhook, &args);

    mlx_loop(windows->mlx);

    cleanup_websocket(&args);
    mlx_terminate(windows->mlx);
}

int main(int argc, char **argv)
{
    void *args;
    screen windows;
    game gameData;
    init_zobrist();

    if (!checkArgs(argc, argv, &args))
        return (EXIT_FAILURE);

    if (!initialized(args, &windows, &gameData))
        return (EXIT_FAILURE);

    launchGame(&gameData, &windows);

    return (EXIT_SUCCESS);
}
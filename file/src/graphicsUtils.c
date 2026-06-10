#include "../include/gomoku.h"

int get_rgba(int r, int g, int b, int a)
{
    return (r << 24 | g << 16 | b << 8 | a);
}

// Dessine le rectangle bleu du bouton (Appelé à chaque redessin du plateau)
void drawResetButton(screen *windows)
{
    int color = get_rgba(70, 130, 180, 255); // Steel Blue
    // int border = get_rgba(255, 255, 255, 255); // Blanc

    // Dessin du fond
    for (int y = BTN_Y; y < BTN_Y + BTN_H; y++)
    {
        for (int x = BTN_X; x < BTN_X + BTN_W; x++)
        {
            if (IS_VALID_PIXEL(x, y, windows)) // Assure-toi d'avoir une macro ou check ici
                mlx_put_pixel(windows->img, x, y, color);
        }
    }
}

// Initialise les éléments de l'interface qui ne changent jamais (le texte du bouton)
void initGUI(screen *windows)
{
    // On s'assure que le texte n'existe pas déjà
    if (windows->restart_text)
        mlx_delete_image(windows->mlx, windows->restart_text);

    // On crée l'image du texte "RESTART" une bonne fois pour toutes
    // On centre un peu le texte (ajustement manuel +10, +5)
    // windows->restart_text = mlx_put_string(windows->mlx, "RESTART", BTN_X + 12, BTN_Y + 5);
}

/* safe pixel put: check boundaries before writing */
static inline void safe_put_pixel(screen *windows, int x, int y, int color)
{
    if (!windows || !windows->img)
        return;
    if (x < 0 || y < 0)
        return;
    if (x >= (int)windows->width || y >= (int)windows->height)
        return;
    mlx_put_pixel(windows->img, x, y, color);
}

void printBlack(screen *windows)
{
    // Remplir de noir (optimisation possible avec memset sur img->pixels si on voulait)
    for (int i = 0; i < (int)windows->width; i++)
    {
        for (int j = 0; j < (int)windows->height; j++)
        {
            safe_put_pixel(windows, i, j, get_rgba(0, 0, 0, 255));
        }
    }
}

void putCadrillage(screen *windows)
{
    if (windows->board_size <= 0)
        return;

    // Ces constantes sont maintenant définies dans gomoku.h
    int ml = BOARD_MARGIN_LEFT;
    int mr = BOARD_MARGIN_RIGHT;
    int mt = BOARD_MARGIN_TOP;
    int mb = BOARD_MARGIN_BOTTOM;

    int drawable_w = (int)windows->width - ml - mr;
    int drawable_h = (int)windows->height - mt - mb;
    if (drawable_w <= 0 || drawable_h <= 0)
        return;

    int color = get_rgba(255, 0, 0, 255);

    /* vertical lines */
    for (int col = 0; col < windows->board_size; col++)
    {
        // Note: j'ai changé col <= à col < car board_size = 19 lignes (index 0 à 18)
        // Mais pour l'affichage grid, c'est parfois board_size lignes.
        // Gardons ta logique d'origine si tu veux les bords.
        
        int x = ml + (int)round((double)col * (double)drawable_w / (double)(windows->board_size - 1));
        // Note: division par (board_size - 1) pour que la dernière ligne soit exactement à la fin
        
        if (x < ml) x = ml;
        if (x > ml + drawable_w) x = ml + drawable_w;
        
        for (int y = mt; y <= mt + drawable_h; y++)
            safe_put_pixel(windows, x, y, color);
    }

    /* horizontal lines */
    for (int row = 0; row < windows->board_size; row++)
    {
        int y = mt + (int)round((double)row * (double)drawable_h / (double)(windows->board_size - 1));
        
        if (y < mt) y = mt;
        if (y > mt + drawable_h) y = mt + drawable_h;
        
        for (int x = ml; x <= ml + drawable_w; x++)
            safe_put_pixel(windows, x, y, color);
    }
    drawResetButton(windows);
}

int teamColor(unsigned short int team)
{
    switch (team)
    {
    case EMPTY: // 0
        return get_rgba(0, 0, 0, 0); // Transparent pour EMPTY
    case P1:    // 1
        return get_rgba(30, 30, 30, 255); // Noir gomoku
    case P2:    // 2
        return get_rgba(230, 230, 230, 255); // Blanc gomoku
    case 3:     // HINT
        return get_rgba(255, 255, 255, 128); // Semi-transparent
    default:
        return get_rgba(255, 255, 255, 255);
    }
}

void drawSquare(screen *windows, int x0, int y0, unsigned short int team)
{
    if (windows->board_size <= 0)
        return;

    int ml = BOARD_MARGIN_LEFT;
    int mr = BOARD_MARGIN_RIGHT;
    int mt = BOARD_MARGIN_TOP;
    int mb = BOARD_MARGIN_BOTTOM;

    int drawable_w = (int)windows->width - ml - mr;
    int drawable_h = (int)windows->height - mt - mb;
    if (drawable_w <= 0 || drawable_h <= 0)
        return;

    double cell_w_f = (double)drawable_w / (double)(windows->board_size - 1);
    double cell_h_f = (double)drawable_h / (double)(windows->board_size - 1);

    int cx = ml + (int)round(x0 * cell_w_f);
    int cy = mt + (int)round(y0 * cell_h_f);

    int radius = (int)round(fmin(cell_w_f, cell_h_f) * 0.4);
    if (radius < 1) radius = 1;

    if (team == EMPTY)
    {
        // Effacer la zone (un peu plus large que le pion)
        int clear_r = radius + 1;
        for (int i = -clear_r; i <= clear_r; i++)
        {
            for (int j = -clear_r; j <= clear_r; j++)
            {
                safe_put_pixel(windows, cx + i, cy + j, get_rgba(0, 0, 0, 255));
            }
        }
        // Redessiner l'intersection du cadrillage
        int grid_color = get_rgba(255, 0, 0, 255);
        for (int i = -clear_r; i <= clear_r; i++)
            safe_put_pixel(windows, cx + i, cy, grid_color);
        for (int j = -clear_r; j <= clear_r; j++)
            safe_put_pixel(windows, cx, cy + j, grid_color);
        return;
    }

    int color = teamColor(team);
    
    // Dessin d'un cercle (pion)
    for (int i = -radius; i <= radius; i++)
    {
        for (int j = -radius; j <= radius; j++)
        {
            if (i*i + j*j <= radius*radius)
            {
                safe_put_pixel(windows, cx + i, cy + j, color);
            }
        }
    }
}
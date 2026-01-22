/* ═══════════════════════════════════════════════════════════════════════════
 * AI MULTI-THREAT DETECTION
 * 
 * Détecte les situations où l'adversaire a plusieurs formations simultanées
 * qui ne peuvent pas toutes être bloquées en un seul coup.
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "gomoku.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════ */

#define MAX_FORMATIONS 32
#define MAX_STONES_PER_FORMATION 5

typedef struct {
    int stones[MAX_STONES_PER_FORMATION];  /* Index des pierres de la formation */
    int stone_count;                        /* Nombre de pierres */
    int direction;                          /* 0=H, 1=V, 2=D\, 3=D/ */
    int open_ends;                          /* 0, 1 ou 2 bouts ouverts */
    int block_points[2];                    /* Cases pour bloquer */
    int block_count;                        /* Nombre de points de blocage */
    int potential;                          /* Espace total pour atteindre 5 */
} Formation;

typedef struct {
    Formation formations[MAX_FORMATIONS];
    int count;
    int critical_count;      /* Formations de 3+ pierres avec 2 open ends */
    int dangerous_count;     /* Formations de 3+ pierres avec 1+ open end */
    bool is_losing;          /* True si impossible de tout bloquer */
    int best_block;          /* Meilleur coup de blocage global */
    int shared_stones;       /* Nombre de pierres appartenant à 2+ formations */
} MultiThreatAnalysis;

/* ═══════════════════════════════════════════════════════════════════════════
 * FONCTIONS UTILITAIRES
 * ═══════════════════════════════════════════════════════════════════════════ */

static const int DX[] = {1, 0, 1, 1};
static const int DY[] = {0, 1, 1, -1};
static const char* DIR_NAMES[] = {"H", "V", "D\\", "D/"};

/**
 * Vérifie si une pierre est déjà dans une formation
 */
static bool stone_in_formation(Formation *f, int idx) {
    for (int i = 0; i < f->stone_count; i++) {
        if (f->stones[i] == idx) return true;
    }
    return false;
}

/**
 * Vérifie si deux formations partagent au moins une pierre
 */
static bool formations_share_stones(Formation *f1, Formation *f2) {
    for (int i = 0; i < f1->stone_count; i++) {
        for (int j = 0; j < f2->stone_count; j++) {
            if (f1->stones[i] == f2->stones[j]) {
                return true;
            }
        }
    }
    return false;
}

/**
 * Vérifie si un point de blocage peut bloquer plusieurs formations
 */
static int count_formations_blocked_by(MultiThreatAnalysis *analysis, int block_idx) {
    int count = 0;
    for (int i = 0; i < analysis->count; i++) {
        Formation *f = &analysis->formations[i];
        for (int b = 0; b < f->block_count; b++) {
            if (f->block_points[b] == block_idx) {
                count++;
                break;
            }
        }
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCAN DES FORMATIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Scanne une ligne dans une direction donnée et remplit une Formation
 * Retourne true si c'est une formation valide (2+ pierres avec potentiel >= 5)
 */
static bool scan_line_formation(game *g, int start_x, int start_y, int dir, 
                                 int player, Formation *out) {
    int dx = DX[dir];
    int dy = DY[dir];
    
    /* Vérifier qu'on est au début de la ligne */
    int px = start_x - dx;
    int py = start_y - dy;
    if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) {
        return false;  /* Pas le début */
    }
    
    out->stone_count = 0;
    out->direction = dir;
    out->block_count = 0;
    out->open_ends = 0;
    out->potential = 0;
    
    int empty_before = -1;
    int empty_after = -1;
    int space_before = 0;
    int space_after = 0;
    
    /* Compter l'espace AVANT la première pierre */
    for (int k = 1; k <= 4; k++) {
        int bx = start_x - dx * k;
        int by = start_y - dy * k;
        if (!IS_VALID(bx, by)) break;
        
        int cell = g->board[GET_INDEX(bx, by)];
        if (cell == player) break;  /* Pas le début alors */
        if (cell != EMPTY) break;   /* Bloqué par adversaire */
        
        space_before++;
        if (k == 1) empty_before = GET_INDEX(bx, by);
    }
    
    /* Scanner les pierres et l'espace APRÈS */
    int x = start_x;
    int y = start_y;
    
    for (int k = 0; k < 6; k++) {
        if (!IS_VALID(x, y)) break;
        
        int idx = GET_INDEX(x, y);
        int cell = g->board[idx];
        
        if (cell == player) {
            if (out->stone_count < MAX_STONES_PER_FORMATION) {
                out->stones[out->stone_count++] = idx;
            }
        } else if (cell == EMPTY) {
            if (empty_after == -1) {
                empty_after = idx;
            }
            space_after++;
            /* Continuer pour compter l'espace mais ne pas ajouter de pierres */
            if (space_after >= 4) break;
        } else {
            /* Bloqué par adversaire */
            break;
        }
        
        x += dx;
        y += dy;
    }
    
    /* Calculer le potentiel et les bouts ouverts */
    out->potential = out->stone_count + space_before + space_after;
    
    if (empty_before != -1) {
        out->open_ends++;
        out->block_points[out->block_count++] = empty_before;
    }
    if (empty_after != -1) {
        out->open_ends++;
        if (out->block_count < 2) {
            out->block_points[out->block_count++] = empty_after;
        }
    }
    
    /* Valide si 2+ pierres et peut atteindre 5 */
    return (out->stone_count >= 2 && out->potential >= 5);
}

/**
 * Scanne toutes les formations d'un joueur sur le plateau
 */
void scan_all_formations(game *g, int player, MultiThreatAnalysis *analysis) {
    analysis->count = 0;
    analysis->critical_count = 0;
    analysis->dangerous_count = 0;
    analysis->is_losing = false;
    analysis->best_block = -1;
    analysis->shared_stones = 0;
    
    /* Scanner chaque case du plateau */
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        /* Scanner les 4 directions */
        for (int dir = 0; dir < 4; dir++) {
            if (analysis->count >= MAX_FORMATIONS) break;
            
            Formation f;
            if (scan_line_formation(g, x, y, dir, player, &f)) {
                /* Ajouter la formation */
                analysis->formations[analysis->count++] = f;
                
                /* Classifier */
                if (f.stone_count >= 3) {
                    if (f.open_ends >= 2) {
                        analysis->critical_count++;
                    }
                    if (f.open_ends >= 1) {
                        analysis->dangerous_count++;
                    }
                }
            }
        }
    }
    
    /* Compter les pierres partagées entre formations */
    int stone_usage[MAX_BOARD] = {0};
    for (int i = 0; i < analysis->count; i++) {
        Formation *f = &analysis->formations[i];
        for (int j = 0; j < f->stone_count; j++) {
            stone_usage[f->stones[j]]++;
        }
    }
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (stone_usage[idx] >= 2) {
            analysis->shared_stones++;
        }
    }
    
    /* Trouver le meilleur point de blocage (bloque le plus de formations) */
    int best_block_score = 0;
    for (int i = 0; i < analysis->count; i++) {
        Formation *f = &analysis->formations[i];
        for (int b = 0; b < f->block_count; b++) {
            int block_idx = f->block_points[b];
            int score = count_formations_blocked_by(analysis, block_idx);
            
            /* Bonus si la formation a beaucoup de pierres */
            score += f->stone_count * 10;
            
            /* Bonus si c'est une formation critique (2 open ends) */
            if (f->open_ends >= 2 && f->stone_count >= 3) {
                score += 100;
            }
            
            if (score > best_block_score) {
                best_block_score = score;
                analysis->best_block = block_idx;
            }
        }
    }
    
    /* Déterminer si la position est perdante */
    /* Perdant si : 2+ formations critiques (OPEN_THREE ou plus) 
     * qui ne partagent PAS de point de blocage commun */
    if (analysis->critical_count >= 2) {
        /* Vérifier s'il existe un coup qui bloque TOUTES les formations critiques */
        bool can_block_all = false;
        
        /* Collecter tous les points de blocage des formations critiques */
        for (int i = 0; i < analysis->count && !can_block_all; i++) {
            Formation *f1 = &analysis->formations[i];
            if (f1->stone_count < 3 || f1->open_ends < 2) continue;
            
            for (int b = 0; b < f1->block_count && !can_block_all; b++) {
                int candidate = f1->block_points[b];
                
                /* Ce candidat bloque-t-il TOUTES les formations critiques ? */
                bool blocks_all = true;
                for (int j = 0; j < analysis->count && blocks_all; j++) {
                    Formation *f2 = &analysis->formations[j];
                    if (f2->stone_count < 3 || f2->open_ends < 2) continue;
                    
                    bool blocks_this = false;
                    for (int b2 = 0; b2 < f2->block_count; b2++) {
                        if (f2->block_points[b2] == candidate) {
                            blocks_this = true;
                            break;
                        }
                    }
                    if (!blocks_this) blocks_all = false;
                }
                
                if (blocks_all) {
                    can_block_all = true;
                    analysis->best_block = candidate;
                }
            }
        }
        
        analysis->is_losing = !can_block_all;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FONCTION PRINCIPALE D'ANALYSE
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Analyse les multi-menaces adverses et retourne le meilleur coup défensif
 * Retourne -1 si aucune urgence, sinon l'index du coup à jouer
 */
int analyze_multi_threats(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    MultiThreatAnalysis opp_analysis;
    MultiThreatAnalysis my_analysis;
    
    scan_all_formations(g, opponent, &opp_analysis);
    scan_all_formations(g, ia_player, &my_analysis);
    
    #ifdef DEBUG
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           ANALYSE MULTI-FORMATIONS                          ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ ADVERSAIRE: %d formations, %d critiques, %d dangereuses      \n",
           opp_analysis.count, opp_analysis.critical_count, opp_analysis.dangerous_count);
    printf("║ IA:         %d formations, %d critiques, %d dangereuses      \n",
           my_analysis.count, my_analysis.critical_count, my_analysis.dangerous_count);
    printf("║ Pierres partagées (adv): %d                                  \n",
           opp_analysis.shared_stones);
    
    /* Afficher les formations adverses dangereuses */
    for (int i = 0; i < opp_analysis.count; i++) {
        Formation *f = &opp_analysis.formations[i];
        if (f->stone_count >= 2) {
            printf("║   [%d] %d pierres %s, open=%d, pot=%d", 
                   i, f->stone_count, DIR_NAMES[f->direction], 
                   f->open_ends, f->potential);
            if (f->stone_count >= 3 && f->open_ends >= 2) {
                printf(" ⚠️  CRITIQUE");
            }
            printf("\n");
            printf("║       Pierres: ");
            for (int j = 0; j < f->stone_count; j++) {
                printf("(%d,%d) ", GET_X(f->stones[j]), GET_Y(f->stones[j]));
            }
            printf("\n");
            printf("║       Blocages: ");
            for (int b = 0; b < f->block_count; b++) {
                printf("(%d,%d) ", GET_X(f->block_points[b]), GET_Y(f->block_points[b]));
            }
            printf("\n");
        }
    }
    
    if (opp_analysis.is_losing) {
        printf("║                                                              \n");
        printf("║ ⛔ POSITION PERDANTE DÉTECTÉE !                              \n");
        printf("║    Impossible de bloquer toutes les formations critiques     \n");
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    #endif
    
    /* ══════════════════════════════════════════════════════════════════════
     * RÈGLES DE DÉCISION
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* RÈGLE 0 : Si on a une victoire immédiate, jouer */
    for (int i = 0; i < my_analysis.count; i++) {
        Formation *f = &my_analysis.formations[i];
        if (f->stone_count >= 4 && f->open_ends >= 1) {
            /* On peut gagner ! */
            if (f->block_count > 0) {
                int win_move = f->block_points[0];
                if (!is_double_three(g, win_move, ia_player)) {
                    #ifdef DEBUG
                    printf("MULTI-THREAT: Victoire possible en (%d,%d)\n",
                           GET_X(win_move), GET_Y(win_move));
                    #endif
                    return win_move;
                }
            }
        }
    }
    
    /* RÈGLE 1 : Position perdante - jouer le meilleur blocage et espérer */
    if (opp_analysis.is_losing) {
        #ifdef DEBUG
        printf("MULTI-THREAT: Position perdante, blocage désespéré en (%d,%d)\n",
               GET_X(opp_analysis.best_block), GET_Y(opp_analysis.best_block));
        #endif
        return opp_analysis.best_block;
    }
    
    /* RÈGLE 2 : 2+ formations critiques adverses - ALERTE MAXIMALE */
    if (opp_analysis.critical_count >= 2) {
        /* Chercher un coup qui bloque les deux */
        #ifdef DEBUG
        printf("MULTI-THREAT: %d formations critiques ! Blocage urgent en (%d,%d)\n",
               opp_analysis.critical_count, 
               GET_X(opp_analysis.best_block), GET_Y(opp_analysis.best_block));
        #endif
        
        if (opp_analysis.best_block != -1 && 
            !is_double_three(g, opp_analysis.best_block, ia_player)) {
            return opp_analysis.best_block;
        }
    }
    
    /* RÈGLE 3 : 1 formation critique adverse - bloquer si on n'a pas mieux */
    if (opp_analysis.critical_count >= 1) {
        /* Vérifier si on a une contre-attaque valable */
        bool has_counter = false;
        for (int i = 0; i < my_analysis.count; i++) {
            Formation *f = &my_analysis.formations[i];
            /* Contre-attaque valable = formation de 4 pierres OU 
             * formation critique qui gagne avant l'adversaire */
            if (f->stone_count >= 4 || 
                (f->stone_count >= 3 && f->open_ends >= 2)) {
                has_counter = true;
                break;
            }
        }
        
        if (!has_counter && opp_analysis.best_block != -1) {
            if (!is_double_three(g, opp_analysis.best_block, ia_player)) {
                #ifdef DEBUG
                printf("MULTI-THREAT: Formation critique adverse, blocage en (%d,%d)\n",
                       GET_X(opp_analysis.best_block), GET_Y(opp_analysis.best_block));
                #endif
                return opp_analysis.best_block;
            }
        }
    }
    
    /* RÈGLE 4 MODIFIÉE : 2+ formations de 2 pierres avec pierres partagées = DANGER PRÉCOCE */
    if (opp_analysis.count >= 2 && opp_analysis.shared_stones >= 1) {
        /* Compter les bonnes formations de 2 pierres */
        int good_two_formations = 0;
        for (int i = 0; i < opp_analysis.count; i++) {
            Formation *f = &opp_analysis.formations[i];
            if (f->stone_count == 2 && f->open_ends >= 2 && f->potential >= 6) {
                good_two_formations++;
            }
        }
        
        if (good_two_formations >= 2) {
            #ifdef DEBUG
            printf("MULTI-THREAT: %d formations de 2 pierres + %d pierres partagées = DANGER !\n",
                   good_two_formations, opp_analysis.shared_stones);
            printf("MULTI-THREAT: Blocage préventif en (%d,%d)\n",
                   GET_X(opp_analysis.best_block), GET_Y(opp_analysis.best_block));
            #endif
            
            if (opp_analysis.best_block != -1 &&
                !is_double_three(g, opp_analysis.best_block, ia_player)) {
                return opp_analysis.best_block;
            }
        }
    }
    
    /* RÈGLE 5 MODIFIÉE : Pierres partagées avec ANY formation = vigilance */
    if (opp_analysis.shared_stones >= 1 && opp_analysis.count >= 2) {
        #ifdef DEBUG
        printf("MULTI-THREAT: %d pierres partagées avec %d formations\n",
               opp_analysis.shared_stones, opp_analysis.count);
        #endif
        
        if (opp_analysis.best_block != -1 &&
            !is_double_three(g, opp_analysis.best_block, ia_player)) {
            return opp_analysis.best_block;
        }
    }
    
    /* Aucune urgence détectée */
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FONCTION DE PRÉVENTION - À appeler AVANT le TSS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Vérifie si l'adversaire peut développer une multi-formation dangereuse
 * en simulant son prochain coup optimal
 * 
 * Retourne true si on devrait bloquer au lieu de développer
 */
bool should_block_instead_of_develop(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    MultiThreatAnalysis current;
    scan_all_formations(g, opponent, &current);
    
    /* NOUVELLE RÈGLE : Si l'adversaire a des pierres partagées, bloquer */
    if (current.shared_stones >= 1 && current.count >= 2) {
        #ifdef DEBUG
        printf("PRÉVENTION: %d pierres partagées détectées !\n", current.shared_stones);
        #endif
        return true;
    }
    
    /* Si l'adversaire a déjà 2+ formations de 2 pierres avec bon potentiel */
    int good_two_stone_formations = 0;
    for (int i = 0; i < current.count; i++) {
        Formation *f = &current.formations[i];
        if (f->stone_count == 2 && f->open_ends >= 2 && f->potential >= 6) {
            good_two_stone_formations++;
        }
    }
    
    #ifdef DEBUG
    if (good_two_stone_formations >= 2) {
        printf("PRÉVENTION: %d formations de 2 pierres bien placées !\n",
               good_two_stone_formations);
    }
    #endif
    
    return (good_two_stone_formations >= 2);
}
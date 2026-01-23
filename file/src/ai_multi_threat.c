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
    
    /* CORRECTION : Initialiser TOUTE la structure à zéro */
    memset(out, 0, sizeof(Formation));
    
    out->stone_count = 0;
    out->direction = dir;
    out->block_count = 0;
    out->open_ends = 0;
    out->potential = 0;
    out->block_points[0] = -1;  /* AJOUT */
    out->block_points[1] = -1;  /* AJOUT */
    
    /* Initialiser tous les stones à -1 */
    for (int i = 0; i < MAX_STONES_PER_FORMATION; i++) {
        out->stones[i] = -1;
    }
    
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
    
    /* VALIDATION CRITIQUE : Vérifier les bornes avant d'ajouter une pierre */
    #define SAFE_ADD_STONE(idx) do { \
        if ((idx) < 0 || (idx) >= BOARD_SIZE * BOARD_SIZE) { \
            return false; \
        } \
        if (out->stone_count >= MAX_STONES_IN_FORMATION) { \
            return false; \
        } \
        out->stones[out->stone_count++] = (idx); \
    } while(0)
    
    /* VALIDATION FINALE */
    for (int i = 0; i < out->stone_count; i++) {
        if (out->stones[i] < 0 || out->stones[i] >= BOARD_SIZE * BOARD_SIZE) {
            #ifdef DEBUG
            printf("BUG: stone[%d] = %d invalide!\n", i, out->stones[i]);
            #endif
            return false;
        }
    }
    
    for (int i = 0; i < 2; i++) {
        if (out->block_points[i] != -1 && 
            (out->block_points[i] < 0 || out->block_points[i] >= BOARD_SIZE * BOARD_SIZE)) {
            #ifdef DEBUG
            printf("BUG: block_points[%d] = %d invalide!\n", i, out->block_points[i]);
            #endif
            out->block_points[i] = -1;
        }
    }
    
    return (out->stone_count >= 2);
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
    int opp_caps = g->captures[player];
    if (opp_caps >= 2) {
        /* Chercher les cases où l'adversaire peut capturer */
        for (int idx = 0; idx < MAX_BOARD && analysis->count < MAX_FORMATIONS; idx++) {
            if (g->board[idx] != EMPTY) continue;
            
            g->board[idx] = player;
            int caps = count_potential_captures(g, GET_X(idx), GET_Y(idx), player);
            g->board[idx] = EMPTY;
            
            if (caps >= 2) {
                Formation *f = &analysis->formations[analysis->count];
                
                /* CORRECTION : Initialiser TOUS les champs de la formation */
                memset(f, 0, sizeof(Formation));  /* Tout à zéro */
                
                f->stone_count = 0;  /* Pas de pierres pour une menace de capture */
                f->open_ends = 1;
                f->block_points[0] = idx;
                f->block_points[1] = -1;  /* IMPORTANT : Initialiser à -1 */
                f->block_count = 1;
                f->potential = opp_caps + (caps / 2);
                f->direction = -1;  /* Pas de direction pour les captures */
                
                /* NE PAS stocker les pierres (ce n'est pas une formation d'alignement) */
                for (int i = 0; i < MAX_STONES_PER_FORMATION; i++) {
                    f->stones[i] = -1;
                }
                
                /* Classifier comme critique si proche de la victoire */
                if (f->potential >= 4) {
                    analysis->critical_count++;
                }
                if (f->potential >= 3) {
                    analysis->dangerous_count++;
                }
                
                analysis->count++;
            }
        }
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
    MultiThreatAnalysis ia_analysis;
    
    scan_all_formations(g, opponent, &opp_analysis);
    scan_all_formations(g, ia_player, &ia_analysis);
    
    /* VALIDATION POST-SCAN */
    for (int i = 0; i < opp_analysis.count; i++) {
        Formation *f = &opp_analysis.formations[i];
        
        /* Vérifier la cohérence */
        if (f->stone_count < 0 || f->stone_count > MAX_STONES_PER_FORMATION) {
            #ifdef DEBUG
            printf("BUG: Formation[%d] stone_count=%d invalide!\n", i, f->stone_count);
            #endif
            f->stone_count = 0;  /* Invalider cette formation */
            f->block_count = 0;  /* AJOUT : Aussi invalider les blocages */
        }
        
        /* Vérifier les block_points - CORRECTION : utiliser block_count, pas 2 */
        for (int j = 0; j < f->block_count; j++) {
            int bp = f->block_points[j];
            if (bp != -1 && (bp < 0 || bp >= BOARD_SIZE * BOARD_SIZE)) {
                #ifdef DEBUG
                printf("BUG: Formation[%d].block_points[%d]=%d invalide!\n", i, j, bp);
                #endif
                f->block_points[j] = -1;
                
                /* AJOUT : Réduire block_count si on invalide un blocage */
                /* Décaler les blocages valides */
                for (int k = j; k < f->block_count - 1; k++) {
                    f->block_points[k] = f->block_points[k + 1];
                }
                f->block_count--;
                j--;  /* Re-vérifier cette position */
            }
        }
        
        /* AJOUT : Vérifier les pierres aussi */
        for (int j = 0; j < f->stone_count; j++) {
            int stone = f->stones[j];
            if (stone < 0 || stone >= BOARD_SIZE * BOARD_SIZE) {
                #ifdef DEBUG
                printf("BUG: Formation[%d].stones[%d]=%d invalide!\n", i, j, stone);
                #endif
                /* Invalider toute la formation */
                f->stone_count = 0;
                f->block_count = 0;
                break;
            }
        }
    }
    
    /* AJOUT : Même validation pour ia_analysis */
    for (int i = 0; i < ia_analysis.count; i++) {
        Formation *f = &ia_analysis.formations[i];
        
        if (f->stone_count < 0 || f->stone_count > MAX_STONES_PER_FORMATION) {
            f->stone_count = 0;
            f->block_count = 0;
        }
        
        for (int j = 0; j < f->block_count; j++) {
            int bp = f->block_points[j];
            if (bp != -1 && (bp < 0 || bp >= BOARD_SIZE * BOARD_SIZE)) {
                f->block_points[j] = -1;
                for (int k = j; k < f->block_count - 1; k++) {
                    f->block_points[k] = f->block_points[k + 1];
                }
                f->block_count--;
                j--;
            }
        }
        
        for (int j = 0; j < f->stone_count; j++) {
            if (f->stones[j] < 0 || f->stones[j] >= BOARD_SIZE * BOARD_SIZE) {
                f->stone_count = 0;
                f->block_count = 0;
                break;
            }
        }
    }
    
    #ifdef DEBUG
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           ANALYSE MULTI-FORMATIONS                          ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ ADVERSAIRE: %d formations, %d critiques, %d dangereuses      \n",
           opp_analysis.count, opp_analysis.critical_count, opp_analysis.dangerous_count);
    printf("║ IA:         %d formations, %d critiques, %d dangereuses      \n",
           ia_analysis.count, ia_analysis.critical_count, ia_analysis.dangerous_count);
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
    
    /* RÈGLE -1 (PRIORITÉ ABSOLUE) : Victoire adverse par capture */
    int opp_captures = g->captures[opponent];
    if (opp_captures >= 4) {
        int capture_block_idx = -1;
        int capture_danger = compute_capture_danger(g, opponent, &capture_block_idx);
        
        if (capture_danger >= WIN_SCORE && capture_block_idx != -1) {
            #ifdef DEBUG
            printf("URGENCE CAPTURE: Adversaire à 4 paires, blocage en (%d,%d)\n",
                   GET_X(capture_block_idx), GET_Y(capture_block_idx));
            #endif
            return capture_block_idx;
        }
    }

    /* RÈGLE -2 : OPEN_FOUR adverse = Position Perdante (sauf contre-attaque) */
    for (int i = 0; i < opp_analysis.count; i++) {
        Formation *f = &opp_analysis.formations[i];
        
        /* 4 pierres avec 2 bouts ouverts = IMPARABLE */
        if (f->stone_count >= 4 && f->open_ends >= 2) {
            #ifdef DEBUG
            printf("⛔ OPEN_FOUR ADVERSE DÉTECTÉ - Position perdante !\n");
            #endif
            
            /* Chercher une victoire immédiate pour nous */
            int our_win = find_winning_move(g, ia_player);
            if (our_win != -1) {
                return our_win;
            }
            
            /* Chercher une victoire par capture */
            if (g->captures[ia_player] >= 4) {
                int cap = find_best_capture_move(g, ia_player);
                if (cap != -1) {
                    g->board[cap] = ia_player;
                    int caps = count_potential_captures(g, GET_X(cap), GET_Y(cap), ia_player) / 2;
                    g->board[cap] = EMPTY;
                    
                    if (g->captures[ia_player] + caps >= 5) {
                        return cap;  /* Victoire par capture ! */
                    }
                }
            }
            
            /* Aucune contre-attaque → Bloquer un côté (perdu de toute façon) */
            if (f->block_points[0] != -1) {
                return f->block_points[0];
            }
        }
    }

    /* RÈGLE 0 (PRIORITÉ ABSOLUE) : Bloquer victoire imminente (4+ pierres) */
    for (int i = 0; i < opp_analysis.count; i++) {
        Formation *f = &opp_analysis.formations[i];
        
        if (f->stone_count >= 4 && f->open_ends >= 1) {
            /* Chercher un blocage valide (pas double-three) */
            for (int b = 0; b < f->block_count; b++) {
                int block = f->block_points[b];
                if (block != -1 && g->board[block] == EMPTY &&
                    !is_double_three(g, block, ia_player)) {
                    #ifdef DEBUG
                    printf("URGENCE ABSOLUE: Blocage victoire imminente en (%d,%d)\n",
                           GET_X(block), GET_Y(block));
                    #endif
                    return block;
                }
            }
            
            /* Si tous les blocages sont double-three, chercher une alternative */
            #ifdef DEBUG
            printf("ALERTE: Tous les blocages de la formation sont des double-three !\n");
            #endif
            
            /* Chercher une capture qui pourrait sauver */
            int cap = find_best_capture_move(g, ia_player);
            if (cap != -1 && !is_double_three(g, cap, ia_player)) {
                return cap;
            }
        }
    }

    /* RÈGLE 0.3 (NOUVELLE) : CONTRE-ATTAQUE PRIORITAIRE
     * Si on a une formation >= à celle de l'adversaire, ATTAQUER !
     * Sauf si l'adversaire a 4+ pierres (victoire imminente)
     */
    bool opp_has_four = false;
    for (int i = 0; i < opp_analysis.count; i++) {
        if (opp_analysis.formations[i].stone_count >= 4) {
            opp_has_four = true;
            break;
        }
    }

    if (!opp_has_four) {
        /* Comparer notre meilleure formation avec celle de l'adversaire */
        int our_best_stones = 0;
        int our_best_open = 0;
        int our_best_attack = -1;
        
        for (int i = 0; i < ia_analysis.count; i++) {
            Formation *f = &ia_analysis.formations[i];
            if (f->stone_count > our_best_stones ||
                (f->stone_count == our_best_stones && f->open_ends > our_best_open)) {
                our_best_stones = f->stone_count;
                our_best_open = f->open_ends;
                
                /* Trouver le coup d'extension */
                for (int j = 0; j < f->block_count; j++) {
                    int ext = f->block_points[j];
                    if (ext != -1 && g->board[ext] == EMPTY && 
                        !is_double_three(g, ext, ia_player)) {
                        our_best_attack = ext;
                        break;
                    }
                }
            }
        }
        
        int opp_best_stones = 0;
        int opp_best_open = 0;
        for (int i = 0; i < opp_analysis.count; i++) {
            if (opp_analysis.formations[i].stone_count > opp_best_stones) {
                opp_best_stones = opp_analysis.formations[i].stone_count;
                opp_best_open = opp_analysis.formations[i].open_ends;
            }
        }
        
        /* ATTAQUER si on a une formation >= à l'adversaire */
        if (our_best_attack != -1) {
            bool should_attack = false;
            
            /* On a plus de pierres → ATTAQUE */
            if (our_best_stones > opp_best_stones) {
                should_attack = true;
            }
            /* Pierres égales mais on est plus ouvert → ATTAQUE */
            else if (our_best_stones == opp_best_stones && our_best_open > opp_best_open) {
                should_attack = true;
            }
            /* On a 3+ pierres et l'adversaire n'a que des 2 → ATTAQUE */
            else if (our_best_stones >= 3 && opp_best_stones <= 2) {
                should_attack = true;
            }
            
            if (should_attack) {
                #ifdef DEBUG
                printf("CONTRE-ATTAQUE: Nos %d pierres (open=%d) vs leurs %d pierres (open=%d)\n",
                       our_best_stones, our_best_open, opp_best_stones, opp_best_open);
                printf("CONTRE-ATTAQUE: Coup en (%d,%d)\n",
                       GET_X(our_best_attack), GET_Y(our_best_attack));
                #endif
                return our_best_attack;
            }
        }
    }

    /* RÈGLE 0.5 MODIFIÉE : Dès 2 paires, vérifier le danger de capture */
    if (opp_captures >= 2) {
        int capture_block_idx = -1;
        int capture_danger = compute_capture_danger(g, opponent, &capture_block_idx);
        
        /* Si l'adversaire peut capturer ET a déjà 2+ paires */
        if (capture_danger >= OPEN_THREE && capture_block_idx != -1) {
            /* Comparer avec le danger immédiat des alignements */
            bool immediate_alignment_threat = false;
            for (int i = 0; i < opp_analysis.count; i++) {
                if (opp_analysis.formations[i].stone_count >= 4) {
                    immediate_alignment_threat = true;
                    break;
                }
            }
            
            /* Si pas de 4+ pierres adverses, bloquer la capture */
            if (!immediate_alignment_threat) {
                return capture_block_idx;
            }
        }
    }

    /* RÈGLE 1 : Capture défensive qui casse les multi-formations */
    int best_defensive_capture = -1;
    int best_defensive_value = 0;
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        if (count_potential_captures(g, GET_X(idx), GET_Y(idx), ia_player) > 0) {
            int def_value = evaluate_defensive_capture_value(g, idx, ia_player);
            if (def_value > best_defensive_value) {
                if (!is_double_three(g, idx, ia_player)) {
                    best_defensive_value = def_value;
                    best_defensive_capture = idx;
                }
            }
        }
    }
    
    if (best_defensive_capture != -1 && opp_analysis.shared_stones >= 1) {
        #ifdef DEBUG
        printf("CAPTURE DÉFENSIVE: (%d,%d) casse les multi-formations\n",
               GET_X(best_defensive_capture), GET_Y(best_defensive_capture));
        #endif
        return best_defensive_capture;
    }

    /* RÈGLE 2 : Bloquer formations critiques (3 pierres open=2) */
    if (opp_analysis.critical_count >= 1) {
        #ifdef DEBUG
        printf("MULTI-THREAT: Formation critique adverse, blocage en (%d,%d)\n",
               GET_X(opp_analysis.best_block), GET_Y(opp_analysis.best_block));
        #endif
        
        if (opp_analysis.best_block != -1 &&
            !is_double_three(g, opp_analysis.best_block, ia_player)) {
            return opp_analysis.best_block;
        }
    }

    /* RÈGLE 3 : Occupation préemptive SEULEMENT si VRAIMENT pas de danger */
    bool has_dangerous_formation = false;
    for (int i = 0; i < opp_analysis.count; i++) {
        if (opp_analysis.formations[i].stone_count >= 3) {
            has_dangerous_formation = true;
            break;
        }
    }

    if (!has_dangerous_formation &&
        opp_analysis.dangerous_count == 0 && 
        g->captures[opponent] < 3 &&
        opp_analysis.count >= 1 && 
        opp_analysis.count < 3) {
        int junction = find_preemptive_block(g, ia_player);
        if (junction != -1) {
            return junction;
        }
    }

    /* RÈGLE 4 : Multi-formations avec pierres partagées */
    /* RÈGLE 4 MODIFIÉE : 2+ formations de 2 pierres avec pierres partagées = DANGER PRÉCOCE */
    if (opp_analysis.count >= 2 && opp_analysis.shared_stones >= 1) {
        /* NE PAS exécuter si l'adversaire a 2+ paires */
        if (g->captures[opponent] >= 2) {
            #ifdef DEBUG
            printf("RÈGLE 4 ANNULÉE: Adversaire a %d paires\n", g->captures[opponent]);
            #endif
            // Ne pas retourner, continuer vers le Minimax
        } else {
            // Blocage préventif normal
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
    
    /* RÈGLE OFFENSIVE : Si on peut créer une menace >= à celle de l'adversaire */
    if (ia_analysis.critical_count >= 1 || 
        (ia_analysis.dangerous_count >= 2 && opp_analysis.critical_count == 0)) {
        
        /* Trouver le meilleur coup offensif */
        int best_attack = -1;
        int best_attack_score = 0;
        
        for (int i = 0; i < ia_analysis.count; i++) {
            Formation *f = &ia_analysis.formations[i];
            
            /* Chercher un coup qui étend notre meilleure formation */
            for (int j = 0; j < 2; j++) {
                int ext = f->block_points[j];  /* Point d'extension */
                if (ext == -1 || g->board[ext] != EMPTY) continue;
                
                int attack_score = f->stone_count * 1000000 + f->potential * 1000 + f->open_ends * 100;
                
                if (attack_score > best_attack_score) {
                    best_attack_score = attack_score;
                    best_attack = ext;
                }
            }
        }
        
        /* Si notre attaque est meilleure que la défense */
        if (best_attack != -1 && opp_analysis.critical_count == 0) {
            #ifdef DEBUG
            printf("OFFENSIVE: Attaque en (%d,%d) score=%d\n", 
                   GET_X(best_attack), GET_Y(best_attack), best_attack_score);
            #endif
            return best_attack;
        }
    }
    
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
    
    /* RÈGLE 1 : Si l'adversaire a déjà des pierres partagées, bloquer */
    if (current.shared_stones >= 1 && current.count >= 2) {
        #ifdef DEBUG
        printf("PRÉVENTION: %d pierres partagées détectées !\n", current.shared_stones);
        #endif
        return true;
    }
    
    /* RÈGLE 2 (NOUVELLE) : Si une jonction potentielle existe, bloquer */
    int junction = find_preemptive_block(g, ia_player);
    if (junction != -1) {
        #ifdef DEBUG
        printf("PRÉVENTION: Jonction potentielle en (%d,%d)\n",
               GET_X(junction), GET_Y(junction));
        #endif
        return true;
    }
    
    /* RÈGLE 3 : Si l'adversaire a 2+ formations de 2 pierres bien placées */
    int good_two_formations = 0;
    for (int i = 0; i < current.count; i++) {
        Formation *f = &current.formations[i];
        if (f->stone_count == 2 && f->open_ends >= 2 && f->potential >= 6) {
            good_two_formations++;
        }
    }
    
    if (good_two_formations >= 2) {
        #ifdef DEBUG
        printf("PRÉVENTION: %d formations de 2 pierres bien placées !\n",
               good_two_formations);
        #endif
        return true;
    }
    
    return false;
}

/*
 * Calcule si une case est une "jonction potentielle" pour l'adversaire.
 * Une jonction potentielle est une case vide qui, si jouée par l'adversaire,
 * créerait une pierre appartenant à 2+ formations.
 */
int compute_junction_potential(game *g, int idx, int player) {
    if (g->board[idx] != EMPTY) return 0;
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int formations_count = 0;
    
    /* Pour chaque direction, vérifier si placer une pierre ici 
     * créerait/étendrait une formation */
    for (int d = 0; d < 4; d++) {
        int stones_in_line = 0;
        int space_total = 0;
        
        /* Compter dans la direction négative */
        for (int k = 1; k <= 4; k++) {
            int nx = x - dx[d] * k;
            int ny = y - dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            int cell = g->board[GET_INDEX(nx, ny)];
            if (cell == player) stones_in_line++;
            else if (cell == EMPTY) space_total++;
            else break;
        }
        
        /* Compter dans la direction positive */
        for (int k = 1; k <= 4; k++) {
            int nx = x + dx[d] * k;
            int ny = y + dy[d] * k;
            if (!IS_VALID(nx, ny)) break;
            int cell = g->board[GET_INDEX(nx, ny)];
            if (cell == player) stones_in_line++;
            else if (cell == EMPTY) space_total++;
            else break;
        }
        
        /* Si cette direction a au moins 1 pierre alliée et un potentiel de 5 */
        if (stones_in_line >= 1 && stones_in_line + space_total + 1 >= 5) {
            formations_count++;
        }
    }
    
    return formations_count;
}

/*
 * Trouve les cases qui, si occupées par l'IA, empêchent l'adversaire
 * de créer des jonctions.
 */
int find_preemptive_block(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    int best_idx = -1;
    int best_junction_value = 0;
    
    /* Scanner toutes les cases vides */
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        /* Calculer la valeur de jonction pour l'adversaire */
        int junction_value = compute_junction_potential(g, idx, opponent);
        
        /* Si cette case permettrait à l'adversaire de créer 2+ formations */
        if (junction_value >= 2 && junction_value > best_junction_value) {
            /* Vérifier que ce n'est pas un double-three pour nous */
            if (!is_double_three(g, idx, ia_player)) {
                best_junction_value = junction_value;
                best_idx = idx;
                
                #ifdef DEBUG
                printf("JONCTION POTENTIELLE: (%d,%d) value=%d\n",
                       GET_X(idx), GET_Y(idx), junction_value);
                #endif
            }
        }
    }
    
    return best_idx;
}
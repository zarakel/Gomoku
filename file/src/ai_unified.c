#include "../include/gomoku.h"

/*
 * ai_unified.c - Système unifié de détection des menaces
 * 
 * PRINCIPE FONDAMENTAL :
 * Une menace est définie par "combien de coups avant de gagner"
 * PAS par son type (alignement vs capture)
 * 
 * Les fonctions de scan sont dans ai_threat_scan.c et ai_threat_response.c
 */

/* ============================================================================
 * FONCTION PRINCIPALE : SCAN UNIFIÉ
 * ============================================================================ */

int scan_unified_threats(game *g, int player, UnifiedThreat *threats, int max_threats) {
    int count = 0;
    
    /* 1. Scanner les menaces d'alignement du joueur */
    count = scan_alignment_threats(g, player, threats, count, max_threats);
    
    /* 2. Scanner les menaces de capture du joueur */
    count = scan_capture_threats(g, player, threats, count, max_threats);
    
    /* 3. Trier par priorité */
    if (count > 1) {
        qsort(threats, count, sizeof(UnifiedThreat), compare_unified_threats);
    }
    
    #ifdef DEBUG
    printf("=== SCAN UNIFIÉ [%s] : %d menaces ===\n", 
           player == P1 ? "P1" : "P2", count);
    for (int i = 0; i < count && i < 5; i++) {
        char* type_names[] = {"ALIGN", "CAPTURE", "CAP+ALIGN"};
        char* dir_names[] = {"H", "V", "D\\", "D/", "N/A"};
        int dir_idx = (threats[i].direction >= 0) ? threats[i].direction : 4;
        
        printf("  [%d] idx=(%d,%d) score=%d moves=%d type=%s dir=%s stones=%d caps=%d\n",
               i, GET_X(threats[i].index), GET_Y(threats[i].index),
               threats[i].score, threats[i].moves_to_win,
               type_names[threats[i].type], dir_names[dir_idx],
               threats[i].stones, threats[i].captures);
    }
    #endif
    
    return count;
}

/* ============================================================================
 * SCAN UNIFIÉ POUR LES MENACES ADVERSES (À BLOQUER)
 * ============================================================================ */

int scan_unified_opponent_threats(game *g, int ia_player, UnifiedThreat *threats, int max_threats) {
    int opponent = (ia_player == P1) ? P2 : P1;
    int count = 0;
    
    /* Chercher les coups qui donnent WIN_SCORE à l'adversaire */
    for (int i = 0; i < MAX_BOARD && count < max_threats; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, opponent);
        
        if (score >= WIN_SCORE) {
            threats[count].index = i;
            threats[count].score = WIN_SCORE;
            threats[count].stones = 5;
            threats[count].captures = 0;
            threats[count].direction = -1;
            threats[count].type = THREAT_ALIGNMENT;
            threats[count].moves_to_win = MOVES_IMMEDIATE;
            threats[count].is_blocking = true;
            count++;
        }
    }
    
    /* Si on a trouvé des victoires immédiates, les retourner en priorité */
    if (count > 0) {
        return count;
    }
    
    /* 1. Scanner les menaces d'alignement adverses */
    count = scan_alignment_threats(g, opponent, threats, count, max_threats);
    
    /* Marquer comme blocages */
    for (int i = 0; i < count; i++) {
        threats[i].is_blocking = true;
    }
    
    /* 2. Scanner les captures adverses dangereuses */
    count = scan_dangerous_opponent_captures(g, ia_player, threats, count, max_threats);
    
    /* 3. Scanner les menaces de capture adverses simples */
    int capture_start = count;
    count = scan_capture_threats(g, opponent, threats, count, max_threats);
    
    /* Marquer les nouvelles comme blocages */
    for (int i = capture_start; i < count; i++) {
        threats[i].is_blocking = true;
    }
    
    /* 4. Trier par priorité */
    if (count > 1) {
        qsort(threats, count, sizeof(UnifiedThreat), compare_unified_threats);
    }
    
    #ifdef DEBUG
    printf("=== MENACES ADVERSES À BLOQUER : %d ===\n", count);
    for (int i = 0; i < count && i < 5; i++) {
        char* type_names[] = {"ALIGN", "CAPTURE", "CAP+ALIGN"};
        printf("  [%d] idx=(%d,%d) score=%d moves=%d type=%s\n",
               i, GET_X(threats[i].index), GET_Y(threats[i].index),
               threats[i].score, threats[i].moves_to_win,
               type_names[threats[i].type]);
    }
    #endif
    
    return count;
}

/* ============================================================================
 * DÉCISION UNIFIÉE : TROUVE LA MEILLEURE RÉPONSE
 * ============================================================================ */

int get_best_response(game *g, int ia_player, UnifiedThreat *all_threats, int threat_count) {
    (void)all_threats;
    (void)threat_count;
    
    int opponent = (ia_player == P1) ? P2 : P1;

    /* ══════════════════════════════════════════════════════════════════════
     * SCAN DES MENACES (déclaration des variables manquantes)
     * ══════════════════════════════════════════════════════════════════════ */
    
    UnifiedThreat my_threats[MAX_UNIFIED_THREATS];
    UnifiedThreat opp_threats[MAX_UNIFIED_THREATS];
    
    int my_count = scan_unified_threats(g, ia_player, my_threats, MAX_UNIFIED_THREATS);
    int opp_count = scan_unified_opponent_threats(g, ia_player, opp_threats, MAX_UNIFIED_THREATS);
    
    #ifdef DEBUG
    printf("DEBUG UNIFIED: %d menaces IA, %d menaces adverses\n", my_count, opp_count);
    #endif

    /* ══════════════════════════════════════════════════════════════════════
     * PRIORITÉ -1 : ANALYSE MULTI-FORMATIONS (NOUVEAU)
     * 
     * Avant tout, vérifier si l'adversaire a plusieurs formations
     * qui ne peuvent pas toutes être bloquées.
     * ══════════════════════════════════════════════════════════════════════ */
    
    int multi_threat_response = analyze_multi_threats(g, ia_player);
    if (multi_threat_response != -1) {
        #ifdef DEBUG
        printf("DÉCISION MULTI-THREAT: Coup urgent en (%d,%d)\n",
               GET_X(multi_threat_response), GET_Y(multi_threat_response));
        #endif
        return multi_threat_response;
    }

    /* ══════════════════════════════════════════════════════════════════════
     * PRIORITÉ 0 : VICTOIRE IMMÉDIATE
     * ══════════════════════════════════════════════════════════════════════ */
    
    int win_move = find_winning_move(g, ia_player);
    if (win_move != -1) {
        #ifdef DEBUG
        printf("VICTOIRE IMMÉDIATE: coup en (%d,%d)\n", GET_X(win_move), GET_Y(win_move));
        #endif
        return win_move;
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PRIORITÉ 1 : BLOCAGE VICTOIRE ADVERSE
     * 
     * CORRECTION MAJEURE : Scanner TOUTES les cases qui donnent WIN_SCORE
     * à l'adversaire, pas seulement le premier bloc trouvé
     * ══════════════════════════════════════════════════════════════════════ */
    
    int opp_win_moves[10];
    int opp_win_count = 0;
    
    for (int i = 0; i < MAX_BOARD && opp_win_count < 10; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, opponent);
        if (score >= WIN_SCORE) {
            opp_win_moves[opp_win_count++] = i;
            
            #ifdef DEBUG
            printf("MENACE VICTOIRE ADVERSE: (%d,%d) score=%d\n", GET_X(i), GET_Y(i), score);
            #endif
        }
    }
    
    /* Si l'adversaire a UNE seule case gagnante → la bloquer */
    if (opp_win_count == 1) {
        #ifdef DEBUG
        printf("BLOCAGE UNIQUE: (%d,%d)\n", GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
        #endif
        return opp_win_moves[0];
    }
    
    /* Si l'adversaire a DEUX+ cases gagnantes → OPEN_FOUR, chercher contre-attaque */
    if (opp_win_count >= 2) {
        #ifdef DEBUG
        printf("ALERTE: OPEN_FOUR adverse (%d cases gagnantes) !\n", opp_win_count);
        #endif
        
        /* Chercher si on peut gagner par capture */
        if (g->captures[ia_player] >= 4) {
            int cap = find_best_capture_move(g, ia_player);
            if (cap != -1) {
                g->board[cap] = ia_player;
                int caps = count_potential_captures(g, GET_X(cap), GET_Y(cap), ia_player) / 2;
                g->board[cap] = EMPTY;
                
                if (g->captures[ia_player] + caps >= 5) {
                    #ifdef DEBUG
                    printf("VICTOIRE PAR CAPTURE: (%d,%d)\n", GET_X(cap), GET_Y(cap));
                    #endif
                    return cap;
                }
            }
        }
        
        /* Sinon, bloquer une des cases (position perdante) */
        #ifdef DEBUG
        printf("POSITION PERDANTE: blocage en (%d,%d)\n", GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
        #endif
        return opp_win_moves[0];
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PRIORITÉ 1.5 : DÉTECTER LES OPEN_TWO QUI DEVIENNENT OPEN_THREE
     * 
     * Un OPEN_TWO (2 pierres, 2 bouts ouverts, espace pour 5) est dangereux
     * car il devient OPEN_THREE en 1 coup, puis OPEN_FOUR en 2 coups.
     * ══════════════════════════════════════════════════════════════════════ */
    
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    typedef struct {
        int stones;
        int open_ends;
        int blocks[2];
        int block_count;
        int direction;
        int potential;  /* Espace total disponible pour cette ligne */
    } LineInfo;
    
    LineInfo best_opp_line = {0, 0, {-1, -1}, 0, -1, 0};
    LineInfo best_my_line = {0, 0, {-1, -1}, 0, -1, 0};
    
    /* Scanner TOUTES les lignes adverses, même celles de 2 pierres */
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != opponent) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == opponent)
                continue;
            
            int stones = 1;
            int empty_before = -1;
            int empty_after = -1;
            int space_before = 0;
            int space_after = 0;
            
            /* Compter l'espace AVANT */
            for (int k = 1; k <= 4; k++) {
                int bx = x - dx[d] * k;
                int by = y - dy[d] * k;
                if (!IS_VALID(bx, by)) break;
                int cell = g->board[GET_INDEX(bx, by)];
                if (cell == opponent) break;  /* Pas le début de la ligne */
                if (cell == ia_player) break;
                space_before++;
                if (k == 1 && cell == EMPTY) empty_before = GET_INDEX(bx, by);
            }
            
            /* Compter les pierres et l'espace APRÈS */
            for (int k = 1; k <= 5; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == opponent) {
                    stones++;
                } else if (cell == EMPTY) {
                    if (empty_after == -1) empty_after = GET_INDEX(nx, ny);
                    space_after++;
                } else {
                    break;
                }
            }
            
            int open_ends = (empty_before != -1 ? 1 : 0) + (empty_after != -1 ? 1 : 0);
            int potential = stones + space_before + space_after;
            
            /* Ignorer les lignes qui ne peuvent pas atteindre 5 */
            if (potential < 5) continue;
            
            /* Calculer la dangerosité :
             * - 4 pierres OPEN = CRITIQUE
             * - 3 pierres OPEN = TRÈS DANGEREUX  
             * - 2 pierres OPEN avec potentiel 5+ = DANGEREUX
             */
            int danger_score = stones * 100 + open_ends * 50 + potential * 10;
            int current_best = best_opp_line.stones * 100 + best_opp_line.open_ends * 50 + best_opp_line.potential * 10;
            
            if (danger_score > current_best) {
                best_opp_line.stones = stones;
                best_opp_line.open_ends = open_ends;
                best_opp_line.block_count = 0;
                best_opp_line.direction = d;
                best_opp_line.potential = potential;
                
                if (empty_before != -1) {
                    best_opp_line.blocks[best_opp_line.block_count++] = empty_before;
                }
                if (empty_after != -1) {
                    best_opp_line.blocks[best_opp_line.block_count++] = empty_after;
                }
                
                #ifdef DEBUG
                char* dir_names[] = {"H", "V", "D\\", "D/"};
                printf("LIGNE ADV: %d pierres %s, open=%d, potentiel=%d, danger=%d\n",
                       stones, dir_names[d], open_ends, potential, danger_score);
                #endif
            }
        }
    }
    
    /* Scanner nos lignes de la même façon */
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != ia_player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == ia_player)
                continue;
            
            int stones = 1;
            int open_ends = 0;
            int extend_move = -1;
            int space_total = 0;
            
            /* Espace avant */
            for (int k = 1; k <= 4; k++) {
                int bx = x - dx[d] * k;
                int by = y - dy[d] * k;
                if (!IS_VALID(bx, by)) break;
                int cell = g->board[GET_INDEX(bx, by)];
                if (cell == ia_player) break;
                if (cell == opponent) break;
                space_total++;
                if (k == 1 && cell == EMPTY) {
                    open_ends++;
                    if (extend_move == -1) extend_move = GET_INDEX(bx, by);
                }
            }
            
            /* Pierres et espace après */
            for (int k = 1; k <= 5; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == ia_player) {
                    stones++;
                } else if (cell == EMPTY) {
                    space_total++;
                    if (extend_move == -1) extend_move = GET_INDEX(nx, ny);
                    open_ends++;
                    break;
                } else {
                    break;
                }
            }
            
            int potential = stones + space_total;
            if (potential < 5) continue;
            
            int power = stones * 100 + open_ends * 50 + potential * 10;
            int current_best = best_my_line.stones * 100 + best_my_line.open_ends * 50 + best_my_line.potential * 10;
            
            if (power > current_best) {
                best_my_line.stones = stones;
                best_my_line.open_ends = open_ends;
                best_my_line.potential = potential;
                best_my_line.direction = d;
                best_my_line.block_count = 0;
                if (extend_move != -1) {
                    best_my_line.blocks[best_my_line.block_count++] = extend_move;
                }
            }
        }
    }
    
    #ifdef DEBUG
    printf("ANALYSE: ADV=%d pierres (open=%d, pot=%d) vs IA=%d pierres (open=%d, pot=%d)\n",
           best_opp_line.stones, best_opp_line.open_ends, best_opp_line.potential,
           best_my_line.stones, best_my_line.open_ends, best_my_line.potential);
    #endif
    
    /* ══════════════════════════════════════════════════════════════════════
     * RÈGLES DE DÉCISION AMÉLIORÉES
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* RÈGLE 1 : Si adversaire a OPEN_THREE ou plus, BLOQUER IMMÉDIATEMENT */
    if (best_opp_line.stones >= 3 && best_opp_line.open_ends >= 2) {
        /* Sauf si on a une victoire immédiate */
        if (best_my_line.stones >= 4 && best_my_line.open_ends >= 1) {
            if (best_my_line.blocks[0] != -1) {
                #ifdef DEBUG
                printf("CONTRE-ATTAQUE: On a 4+ pierres !\n");
                #endif
                return best_my_line.blocks[0];
            }
        }
        
        /* Bloquer l'OPEN_THREE */
        if (best_opp_line.blocks[0] != -1) {
            #ifdef DEBUG
            printf("BLOCAGE OPEN_THREE: (%d,%d)\n", 
                   GET_X(best_opp_line.blocks[0]), GET_Y(best_opp_line.blocks[0]));
            #endif
            return best_opp_line.blocks[0];
        }
    }
    
    /* RÈGLE 2 : Si adversaire a OPEN_TWO avec bon potentiel ET on n'a rien de mieux */
    if (best_opp_line.stones == 2 && best_opp_line.open_ends >= 2 && best_opp_line.potential >= 5) {
        /* Si on a aussi une bonne ligne, on développe */
        if (best_my_line.stones >= 3 || 
            (best_my_line.stones == 2 && best_my_line.open_ends >= 2)) {
            if (best_my_line.blocks[0] != -1) {
                #ifdef DEBUG
                printf("DÉVELOPPEMENT IA: (%d,%d)\n",
                       GET_X(best_my_line.blocks[0]), GET_Y(best_my_line.blocks[0]));
                #endif
                return best_my_line.blocks[0];
            }
        }
        /* Sinon, perturber la ligne adverse */
        else if (best_opp_line.blocks[0] != -1) {
            #ifdef DEBUG
            printf("PERTURBATION OPEN_TWO: (%d,%d)\n",
                   GET_X(best_opp_line.blocks[0]), GET_Y(best_opp_line.blocks[0]));
            #endif
            /* Ne pas retourner directement, juste noter comme option */
        }
    }
    
    /* RÈGLE 3 : Blocage des lignes de 3+ pierres (même CLOSED) */
    if (best_opp_line.stones >= 3 && best_opp_line.blocks[0] != -1) {
        if (best_my_line.stones >= 3 && best_my_line.blocks[0] != -1) {
            /* On a aussi une menace, développer si plus avancée */
            if (best_my_line.stones > best_opp_line.stones ||
                (best_my_line.stones == best_opp_line.stones && 
                 best_my_line.open_ends > best_opp_line.open_ends)) {
                return best_my_line.blocks[0];
            }
        }
        return best_opp_line.blocks[0];
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PAS DE MENACE URGENTE : Utiliser le scan unifié ou développer
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* Utiliser les menaces du scan unifié */
    for (int i = 0; i < my_count; i++) {
        if (my_threats[i].moves_to_win <= MOVES_NEXT) {
            #ifdef DEBUG
            printf("MENACE IA: coup en (%d,%d)\n", 
                   GET_X(my_threats[i].index), GET_Y(my_threats[i].index));
            #endif
            return my_threats[i].index;
        }
    }
    
    for (int i = 0; i < opp_count; i++) {
        if (opp_threats[i].moves_to_win <= MOVES_NEXT) {
            #ifdef DEBUG
            printf("BLOCAGE MENACE: coup en (%d,%d)\n", 
                   GET_X(opp_threats[i].index), GET_Y(opp_threats[i].index));
            #endif
            return opp_threats[i].index;
        }
    }
    
    /* ══════════════════════════════════════════════════════════════════════
     * PRIORITÉ 4 : TSS (Threat Space Search)
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* Vérification préventive : l'adversaire construit-il plusieurs formations ? */
    /* ══════════════════════════════════════════════════════════════════════
     * NOUVELLE VÉRIFICATION : Multi-formations adverses AVANT le TSS
     * ══════════════════════════════════════════════════════════════════════ */
    
    /* Si l'adversaire a des pierres partagées ou 2+ formations, NE PAS utiliser le TSS */
    if (should_block_instead_of_develop(g, ia_player)) {
        #ifdef DEBUG
        printf("TSS ANNULÉ: Multi-formations adverses détectées\n");
        #endif
        
        /* Chercher le meilleur blocage via analyze_multi_threats */
        int block = analyze_multi_threats(g, ia_player);
        if (block != -1) {
            return block;
        }
        
        /* Sinon, passer au Minimax */
        return -1;
    }

    /* TSS normal uniquement si pas de danger multi-formation */
    clock_t start = clock();
    int tss_move = tss_find_winning_sequence(g, ia_player, start, 50);
    if (tss_move != -1) {
        if (!is_double_three(g, tss_move, ia_player)) {
            #ifdef DEBUG
            printf("TSS: Séquence gagnante trouvée ! Coup: (%d, %d)\n",
                   GET_X(tss_move), GET_Y(tss_move));
            #endif
            return tss_move;
        }
    }
    
    return -1;  /* Passer au Minimax */
}

/* ============================================================================
 * FONCTION HELPER : VALIDATION DU COUP
 * ============================================================================ */

static int validate_and_return(game *g, int move, int player) {
    if (move == -1) return -1;
    if (g->board[move] != EMPTY) return -1;
    
    /* Vérifier si c'est un coup interdit */
    if (is_double_three(g, move, player)) {
        #ifdef DEBUG
        printf("⚠️  Coup (%d,%d) interdit (double-three), recherche alternative\n",
               GET_X(move), GET_Y(move));
        #endif
        return -1;  /* Forcer la recherche d'une alternative */
    }
    
    return move;
}
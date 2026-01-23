#include "../include/gomoku.h"

/*
 * ai_decision.c - Module de décision simplifié
 * 
 * ARCHITECTURE MINIMAX-FIRST :
 * Seules les urgences absolues (victoire/défaite immédiate) court-circuitent le Minimax.
 * Tout le reste est délégué au Minimax qui a une vue à plusieurs coups d'avance.
 */

/* ============================================================================
 * DÉTECTION VICTOIRE IMMÉDIATE (ALIGNEMENT)
 * ============================================================================ */

static int find_winning_alignment(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        if (is_double_three(g, i, player)) continue;
        
        g->board[i] = player;
        int score = get_point_score(g, GET_X(i), GET_Y(i), player);
        g->board[i] = EMPTY;
        
        if (score >= WIN_SCORE) return i;
    }
    return -1;
}

/* ============================================================================
 * DÉTECTION VICTOIRE IMMÉDIATE (CAPTURE)
 * ============================================================================ */

static int find_winning_capture(game *g, int player) {
    if (g->captures[player] < 4) return -1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = player;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), player) / 2;
        g->board[i] = EMPTY;
        
        if (g->captures[player] + caps >= 5) return i;
    }
    return -1;
}

/* ============================================================================
 * NOUVELLE FONCTION : Estimation du nombre de coups pour gagner
 * Retourne le nombre minimum de coups pour atteindre WIN_SCORE
 * ============================================================================ */

static int estimate_moves_to_win(game *g, int player) {
    int best_threat = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        if (is_double_three(g, i, player)) continue;
        
        g->board[i] = player;
        int score = get_point_score(g, GET_X(i), GET_Y(i), player);
        g->board[i] = EMPTY;
        
        if (score > best_threat) best_threat = score;
    }
    
    // Conversion score → nombre de coups estimé
    if (best_threat >= WIN_SCORE) return 1;      // Victoire immédiate
    if (best_threat >= OPEN_FOUR) return 1;      // Victoire au prochain coup
    if (best_threat >= CLOSED_FOUR) return 2;    // Force le blocage, puis on gagne
    if (best_threat >= OPEN_THREE) return 2;     // Devient OPEN_FOUR, puis WIN
    if (best_threat >= CLOSED_THREE) return 3;   // Progression normale
    if (best_threat >= OPEN_TWO) return 4;
    return 10;  // Loin de la victoire
}

/* ============================================================================
 * NOUVELLE FONCTION : Trouver le meilleur blocage d'une menace critique
 * ============================================================================ */

static int find_critical_block(game *g, int opponent, int threat_level) {
    int best_block = -1;
    int best_reduction = 0;
    int ia_player = (opponent == P1) ? P2 : P1;
    
    // Trouver toutes les cases qui réduisent la menace adverse
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        if (is_double_three(g, i, ia_player)) continue;
        
        // Simuler notre coup
        g->board[i] = ia_player;
        
        // Recalculer la meilleure menace adverse après notre coup
        int new_opp_threat = 0;
        for (int j = 0; j < MAX_BOARD; j++) {
            if (g->board[j] != EMPTY) continue;
            
            g->board[j] = opponent;
            int score = get_point_score(g, GET_X(j), GET_Y(j), opponent);
            g->board[j] = EMPTY;
            
            if (score > new_opp_threat) new_opp_threat = score;
        }
        
        // Calculer aussi notre propre attaque (pour les coups mixtes)
        int our_attack = get_point_score(g, GET_X(i), GET_Y(i), ia_player);
        
        g->board[i] = EMPTY;
        
        int reduction = threat_level - new_opp_threat;
        
        // Bonus pour coups qui attaquent aussi
        int combined_score = reduction;
        if (our_attack >= OPEN_THREE) combined_score += OPEN_THREE / 2;
        if (our_attack >= CLOSED_FOUR) combined_score += CLOSED_FOUR;
        if (our_attack >= OPEN_FOUR) combined_score += OPEN_FOUR;
        
        if (combined_score > best_reduction) {
            best_reduction = combined_score;
            best_block = i;
        }
    }
    
    return best_block;
}

/* ============================================================================
 * DÉTECTION DÉFENSE OBLIGATOIRE (AMÉLIORÉE)
 * 
 * Détecte :
 * 1. Victoires immédiates adverses (WIN_SCORE)
 * 2. OPEN_FOUR adverses (victoire au prochain coup)
 * 3. CLOSED_FOUR adverses (nécessite blocage)
 * 4. Course à la victoire (qui gagne en premier ?)
 * ============================================================================ */

static int find_mandatory_defense(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    int opp_win_moves[16];
    int opp_win_count = 0;
    int opp_best_threat = 0;
    int opp_best_threat_idx = -1;
    
    /* 1. Scanner TOUTES les menaces adverses */
    for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        // Victoire immédiate par alignement
        if (score >= WIN_SCORE) {
            opp_win_moves[opp_win_count++] = i;
        }
        
        // Tracker la meilleure menace
        if (score > opp_best_threat) {
            opp_best_threat = score;
            opp_best_threat_idx = i;
        }
    }
    
    /* 2. Victoires par capture */
    if (g->captures[opponent] >= 4) {
        for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
            if (g->board[i] != EMPTY) continue;
            
            bool already = false;
            for (int k = 0; k < opp_win_count; k++) {
                if (opp_win_moves[k] == i) { already = true; break; }
            }
            if (already) continue;
            
            g->board[i] = opponent;
            int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent) / 2;
            g->board[i] = EMPTY;
            
            if (g->captures[opponent] + caps >= 5) {
                opp_win_moves[opp_win_count++] = i;
            }
        }
    }
    
    /* 3. Si victoire immédiate adverse → bloquer ou contre-attaquer */
    if (opp_win_count > 0) {
        if (opp_win_count == 1) {
            #ifdef DEBUG
            printf("DEFENSE: Blocage unique en (%d,%d)\n", 
                   GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
            #endif
            return opp_win_moves[0];
        }
        
        // Plusieurs menaces → chercher contre-attaque
        #ifdef DEBUG
        printf("ALERTE: %d cases gagnantes adverses ! Recherche contre-attaque...\n", opp_win_count);
        #endif
        
        int my_win = find_winning_alignment(g, ia_player);
        if (my_win != -1) return my_win;
        
        int my_cap_win = find_winning_capture(g, ia_player);
        if (my_cap_win != -1) return my_cap_win;
        
        #ifdef DEBUG
        printf("POSITION PERDANTE: blocage en (%d,%d)\n", 
               GET_X(opp_win_moves[0]), GET_Y(opp_win_moves[0]));
        #endif
        return opp_win_moves[0];
    }
    
    /* ═══════════════════════════════════════════════════════════════════
     * NOUVEAU : Détection des menaces critiques (OPEN_FOUR, CLOSED_FOUR)
     * ═══════════════════════════════════════════════════════════════════ */
    
    /* 4. OPEN_FOUR adverse = victoire au prochain coup si non bloqué */
    if (opp_best_threat >= OPEN_FOUR) {
        #ifdef DEBUG
        printf("ALERTE CRITIQUE: OPEN_FOUR adverse détecté (score=%d) !\n", opp_best_threat);
        #endif
        
        // Peut-on gagner immédiatement ?
        int my_win = find_winning_alignment(g, ia_player);
        if (my_win != -1) return my_win;
        
        int my_cap_win = find_winning_capture(g, ia_player);
        if (my_cap_win != -1) return my_cap_win;
        
        // Sinon, bloquer la menace
        int block = find_critical_block(g, opponent, opp_best_threat);
        if (block != -1) {
            #ifdef DEBUG
            printf("BLOCAGE OPEN_FOUR en (%d,%d)\n", GET_X(block), GET_Y(block));
            #endif
            return block;
        }
        
        // Fallback : bloquer directement la case menaçante
        return opp_best_threat_idx;
    }
    
    /* 5. CLOSED_FOUR adverse = DOIT être bloqué (4 pierres !) */
    if (opp_best_threat >= CLOSED_FOUR) {
        #ifdef DEBUG
        printf("ALERTE: CLOSED_FOUR adverse détecté (score=%d)\n", opp_best_threat);
        #endif
        
        // Un CLOSED_FOUR avec 4 pierres = victoire adverse si non bloqué !
        // Peut-on créer une menace SUPÉRIEURE (OPEN_FOUR ou WIN) ?
        int my_best_threat = 0;
        int my_best_idx = -1;
        for (int i = 0; i < MAX_BOARD; i++) {
            if (g->board[i] != EMPTY) continue;
            if (is_double_three(g, i, ia_player)) continue;
            
            g->board[i] = ia_player;
            int score = get_point_score(g, GET_X(i), GET_Y(i), ia_player);
            g->board[i] = EMPTY;
            
            if (score > my_best_threat) {
                my_best_threat = score;
                my_best_idx = i;
            }
        }
        
        // Si on peut créer un OPEN_FOUR ou gagner, on attaque
        if (my_best_threat >= OPEN_FOUR) {
            #ifdef DEBUG
            printf("CONTRE-ATTAQUE: Notre OPEN_FOUR (%d) > leur CLOSED_FOUR\n", my_best_threat);
            #endif
            return my_best_idx;  // Jouer notre attaque supérieure
        }
        
        // Sinon, BLOQUER OBLIGATOIREMENT
        int block = find_critical_block(g, opponent, opp_best_threat);
        if (block != -1) {
            #ifdef DEBUG
            printf("BLOCAGE CLOSED_FOUR en (%d,%d)\n", GET_X(block), GET_Y(block));
            #endif
            return block;
        }
        
        // Fallback : bloquer la case menaçante directement
        if (opp_best_threat_idx != -1) {
            return opp_best_threat_idx;
        }
    }
    
    /* ═══════════════════════════════════════════════════════════════════
     * NOUVEAU : Course à la victoire
     * ═══════════════════════════════════════════════════════════════════ */
    
    /* 6. Vérifier qui gagne la "course" */
    int my_moves_to_win = estimate_moves_to_win(g, ia_player);
    int opp_moves_to_win = estimate_moves_to_win(g, opponent);
    
    #ifdef DEBUG
    printf("COURSE: IA=%d coups, Adversaire=%d coups\n", my_moves_to_win, opp_moves_to_win);
    #endif
    
    // Si l'adversaire gagne avant nous ET a une menace sérieuse → défendre
    if (opp_moves_to_win <= my_moves_to_win && opp_best_threat >= OPEN_THREE) {
        #ifdef DEBUG
        printf("ALERTE COURSE: L'adversaire gagne en %d coups, nous en %d. DEFENSE!\n",
               opp_moves_to_win, my_moves_to_win);
        #endif
        
        int block = find_critical_block(g, opponent, opp_best_threat);
        if (block != -1) return block;
    }
    
    /* Aucune urgence détectée */
    return -1;
}

/* ============================================================================
 * FONCTION PRINCIPALE : DÉCISION IA
 * ============================================================================ */

int make_tactical_decision(game *g, int ia_player, clock_t start_time) {
    (void)start_time;
    
    /* RÈGLE 1 : VICTOIRE IMMÉDIATE */
    int win_align = find_winning_alignment(g, ia_player);
    if (win_align != -1) {
        #ifdef DEBUG
        printf("VICTOIRE: Alignement gagnant en (%d,%d)\n", 
               GET_X(win_align), GET_Y(win_align));
        #endif
        return win_align;
    }
    
    int win_capture = find_winning_capture(g, ia_player);
    if (win_capture != -1) {
        #ifdef DEBUG
        printf("VICTOIRE: Capture gagnante en (%d,%d)\n", 
               GET_X(win_capture), GET_Y(win_capture));
        #endif
        return win_capture;
    }
    
    /* RÈGLE 2 : DÉFENSE OBLIGATOIRE (améliorée) */
    int defense = find_mandatory_defense(g, ia_player);
    if (defense != -1) {
        return defense;
    }
    
    /* RÈGLE 3 : MINIMAX DÉCIDE */
    #ifdef DEBUG
    printf("DECISION: Pas d'urgence → Minimax\n");
    #endif
    
    return -1;
}
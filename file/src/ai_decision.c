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

static int find_mandatory_defense(game *g, int ia_player) {
    int opponent = (ia_player == P1) ? P2 : P1;
    
    int opp_win_moves[16];
    int opp_win_count = 0;
    
    /* 1. Scanner TOUTES les menaces adverses (Alignement) */
    for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (score >= WIN_SCORE) {
            opp_win_moves[opp_win_count++] = i;
        }
    }
    
    /* 2. Victoires par capture */
    if (g->captures[opponent] >= 4) {
        for (int i = 0; i < MAX_BOARD && opp_win_count < 16; i++) {
            if (g->board[i] != EMPTY) continue;
            
            // Éviter doublons
            bool already = false;
            for (int k = 0; k < opp_win_count; k++) { if (opp_win_moves[k] == i) already = true; }
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
        // Si une seule menace, on bloque bêtement (pas le choix)
        if (opp_win_count == 1) return opp_win_moves[0];
        
        // Si plusieurs menaces, c'est perdu SAUF si on peut gagner NOUS-MÊMES tout de suite
        int my_win = find_winning_alignment(g, ia_player);
        if (my_win != -1) return my_win;
        
        int my_cap_win = find_winning_capture(g, ia_player);
        if (my_cap_win != -1) return my_cap_win;
        
        return opp_win_moves[0]; // On bloque le premier par désespoir
    }
    
    /* 4. Course à la victoire (Optionnel mais ok à garder) */
    int my_moves_to_win = estimate_moves_to_win(g, ia_player);
    int opp_moves_to_win = estimate_moves_to_win(g, opponent);
    
    // Si l'adversaire est plus rapide et très menaçant
    // Note : Ici on utilise find_critical_block qui doit être définie plus haut
    int opp_best_threat = 0; // Il faudrait recalculer ou passer opp_best_threat en paramètre si on veut optimiser
    // Pour simplifier, on peut retirer cette partie si elle est trop lourde, 
    // ou la garder si find_critical_block est fiable.
    
    return -1; // Pas d'urgence absolue -> Minimax
}

/* * Wrapper pour trouver le coup qui déclenche le VCF
 */
static int find_vcf_move(game *g, int ia_player, clock_t start_time) {
    // On génère les coups prometteurs
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, ia_player, 0, -1);
    
    for (int i = 0; i < count; i++) {
        // Optimisation : On ne teste que les coups d'attaque (Open 3 ou mieux)
        if (moves[i].score_estim < OPEN_THREE) break;
        
        int idx = moves[i].index;
        MoveUndo undo;
        apply_move(g, idx, ia_player, &undo);
        
        // Si je joue 'idx', est-ce que je gagne par VCF ?
        // Note: On réduit la profondeur car on a déjà joué un coup
        bool wins = has_vcf_win(g, ia_player, 1, 12, start_time);
        
        undo_move(g, ia_player, &undo);
        
        if (wins) return idx; // On a trouvé le coup déclencheur !
    }
    return -1;
}

int make_tactical_decision(game *g, int ia_player, clock_t start_time) {
    (void)start_time;

    // 1. VICTOIRE IMMÉDIATE (Absolue priorité)
    int win_align = find_winning_alignment(g, ia_player);
    if (win_align != -1) return win_align;

    int win_capture = find_winning_capture(g, ia_player);
    if (win_capture != -1) return win_capture;

    // 2. DÉFAITE IMMÉDIATE (Si l'adversaire gagne au prochain coup)
    // On laisse le Minimax gérer le blocage SAUF si on peut contre-attaquer par VCF.
    // C'est la seule "Règle" utile de l'alternative : Counter-Attack > Block.
    
    int opponent = (ia_player == P1) ? P2 : P1;
    int opp_threat_lvl = g->max_threat_level[opponent]; // Utiliser vos buckets
    
    // Si l'adversaire menace de gagner (Open 4 ou Win), on tente le VCF avant de bloquer
    if (opp_threat_lvl >= IDX_OPEN_FOUR) {
        int my_threat_lvl = g->max_threat_level[ia_player];
        
        // Si j'ai aussi du potentiel, je tente le "VCF Counter"
        if (my_threat_lvl >= IDX_OPEN_THREE) {
             int vcf_move = find_vcf_move(g, ia_player, start_time);
             if (vcf_move != -1) return vcf_move; // La meilleure défense c'est l'attaque
        }
        
        // Si pas de VCF, on retourne -1 -> Le Minimax trouvera le meilleur blocage
        return -1; 
    }

    // 3. VCF OFFENSIF (Si pas de menace immédiate adverse)
    // On tente de finir la partie si on a une ouverture
    int my_threat_lvl = g->max_threat_level[ia_player];
    
    if (my_threat_lvl >= IDX_OPEN_THREE) { 
        int vcf_move = find_vcf_move(g, ia_player, start_time);
        if (vcf_move != -1) return vcf_move;
    }

    // 4. Sinon, laisser le Minimax faire son travail positionnel
    return -1; 
}
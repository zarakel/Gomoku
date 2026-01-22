#include "../include/gomoku.h"

/*
 * tss.c - Threat Space Search
 * 
 * Recherche spécialisée pour trouver des séquences de menaces gagnantes.
 * Contrairement au Minimax, le TSS ne considère que les coups "de menace"
 * et les réponses forcées de l'adversaire.
 * 
 * Complexité réduite : O(menaces^profondeur) au lieu de O(coups^profondeur)
 */

/* ============================================================================
 * STRUCTURES INTERNES
 * ============================================================================ */

typedef struct {
    int index;      /* Case du coup */
    int score;      /* Score de la menace (CLOSED_THREE, OPEN_FOUR, etc.) */
} ThreatMove;

/* ============================================================================
 * GÉNÉRATION DES COUPS DE MENACE
 * Ne retourne que les coups qui créent une menace >= min_threat
 * ============================================================================ */

static int generate_threat_moves(game *g, int player, ThreatMove *threats, int max_threats, int min_threat) {
    int count = 0;
    
    for (int i = 0; i < MAX_BOARD && count < max_threats; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // CORRECTION : Utiliser evaluate_move_with_captures_full au lieu de get_point_score
        int score = evaluate_move_with_captures_full(g, i, player);
        
        // Vérifier aussi les captures qui mènent à 5 paires
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), player);
        if (g->captures[player] + caps / 2 >= 5) {
            score = WIN_SCORE;
        }
        
        // NOUVEAU : Bonus si la capture crée une menace indirecte
        if (caps >= 2) {
            // Simuler la capture pour voir ce qui se passe après
            MoveUndo undo;
            apply_move(g, i, player, &undo);
            
            // Scanner les nouvelles opportunités après capture
            for (int j = 0; j < MAX_BOARD; j++) {
                if (g->board[j] != EMPTY) continue;
                int follow_up_score = evaluate_move_with_captures_full(g, j, player);
                if (follow_up_score >= OPEN_FOUR) {
                    // La capture ouvre une menace gagnante !
                    score = OPEN_FOUR + (caps * 1000);
                    break;
                }
            }
            
            undo_move(g, player, &undo);
        }
        
        if (score >= min_threat) {
            threats[count].index = i;
            threats[count].score = score;
            count++;
        }
    }
    
    // Tri par score décroissant
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (threats[j].score > threats[i].score) {
                ThreatMove tmp = threats[i];
                threats[i] = threats[j];
                threats[j] = tmp;
            }
        }
    }
    
    return count;
}

/*
 * NOUVELLE FONCTION : Détecte si une capture adverse créerait une menace sérieuse
 * Retourne l'index de la case dangereuse, ou -1 si pas de danger
 * 
 * Différence avec detect_capture_to_double_threat :
 * - Celle-ci détecte >= 2 menaces (double menace imparable)
 * - La nouvelle détecte >= 1 menace CLOSED_FOUR ou mieux (menace simple mais critique)
 */
int detect_capture_creates_threat(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    int best_danger_idx = -1;
    int best_danger_score = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* L'adversaire peut-il capturer ici ? */
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (caps < 2) continue; /* Pas de capture possible */
        
        /* Simuler la capture COMPLÈTE */
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        /* Chercher la meilleure menace APRÈS la capture */
        int best_threat_after = 0;
        int best_threat_idx = -1;
        
        for (int j = 0; j < MAX_BOARD; j++) {
            if (g->board[j] != EMPTY) continue;
            
            /* Évaluer ce que l'adversaire obtient en jouant là */
            int score = evaluate_move_with_captures_full(g, j, opponent);
            
            if (score > best_threat_after) {
                best_threat_after = score;
                best_threat_idx = j;
            }
        }
        
        /* AUSSI : Scanner les menaces EXISTANTES après capture */
        ExistingThreat threats[10];
        int threat_count = scan_all_existing_threats(g, opponent, threats, 10);
        
        for (int t = 0; t < threat_count; t++) {
            if (threats[t].score > best_threat_after) {
                best_threat_after = threats[t].score;
            }
        }
        
        undo_move(g, opponent, &undo);
        
        /* Si la capture crée une menace >= CLOSED_FOUR, c'est TRÈS dangereux */
        if (best_threat_after >= CLOSED_FOUR && best_threat_after > best_danger_score) {
            best_danger_score = best_threat_after;
            best_danger_idx = i;
            
            #ifdef DEBUG
            printf("ALERTE: Capture adverse en (%d,%d) créerait menace score=%d !\n",
                   GET_X(i), GET_Y(i), best_threat_after);
            #endif
        }
    }
    
    return best_danger_idx;
}

int detect_capture_to_double_threat(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (caps < 2) continue;
        
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        int threats_after = 0;
        for (int j = 0; j < MAX_BOARD && threats_after < 2; j++) {
            if (g->board[j] != EMPTY) continue;
            int score = evaluate_move_with_captures_full(g, j, opponent);
            if (score >= CLOSED_FOUR) threats_after++;
        }
        
        undo_move(g, opponent, &undo);
        
        if (threats_after >= 2) {
            #ifdef DEBUG
            printf("ALERTE: Capture adverse en (%d,%d) créerait %d menaces !\n",
                   GET_X(i), GET_Y(i), threats_after);
            #endif
            return i;
        }
    }
    
    return -1;
}

/* ============================================================================
 * RECHERCHE DES RÉPONSES FORCÉES
 * Trouve les cases où l'adversaire DOIT jouer pour bloquer une menace
 * ============================================================================ */

static int find_forced_responses(game *g, int defender, int threat_idx, int threat_score, int *responses, int max_responses) {
    int count = 0;
    int attacker = (defender == P1) ? P2 : P1;
    
    // NOUVEAU : Si la menace a été créée par une capture, vérifier si on peut
    // "défaire" la capture en capturant à notre tour
    
    // Chercher les captures défensives qui pourraient annuler la menace
    for (int i = 0; i < MAX_BOARD && count < max_responses; i++) {
        if (g->board[i] != EMPTY) continue;
        
        g->board[i] = defender;
        int def_caps = count_potential_captures(g, GET_X(i), GET_Y(i), defender);
        g->board[i] = EMPTY;
        
        if (def_caps >= 2) {
            // Simuler notre capture
            MoveUndo undo;
            apply_move(g, i, defender, &undo);
            
            // La menace existe-t-elle encore ?
            int remaining_threat = evaluate_move_with_captures_full(g, threat_idx, attacker);
            
            undo_move(g, defender, &undo);
            
            if (remaining_threat < threat_score) {
                // Notre capture réduit la menace !
                responses[count++] = i;
            }
        }
    }
    
    /* Si la menace est WIN_SCORE, pas de réponse possible (sauf capture) */
    if (threat_score >= WIN_SCORE) {
        /* Chercher si une capture peut sauver */
        for (int i = 0; i < MAX_BOARD && count < max_responses; i++) {
            if (g->board[i] != EMPTY) continue;
            
            g->board[i] = defender;
            int caps = count_potential_captures(g, GET_X(i), GET_Y(i), defender);
            g->board[i] = EMPTY;
            
            /* La capture retire-t-elle la menace ? */
            if (caps > 0) {
                /* Simuler la capture et vérifier */
                MoveUndo undo;
                apply_move(g, i, defender, &undo);
                
                int remaining_threat = get_point_score(g, GET_X(threat_idx), GET_Y(threat_idx), attacker);
                
                undo_move(g, defender, &undo);
                
                if (remaining_threat < WIN_SCORE) {
                    responses[count++] = i;
                }
            }
        }
        return count; /* 0 si pas de capture salvatrice = double menace ! */
    }
    
    /* Si la menace est OPEN_FOUR, pas de réponse (sauf si on a aussi OPEN_FOUR) */
    if (threat_score >= OPEN_FOUR) {
        /* Vérifier si le défenseur peut créer un OPEN_FOUR ou WIN */
        for (int i = 0; i < MAX_BOARD && count < max_responses; i++) {
            if (g->board[i] != EMPTY) continue;
            
            g->board[i] = defender;
            int def_score = get_point_score(g, GET_X(i), GET_Y(i), defender);
            g->board[i] = EMPTY;
            
            if (def_score >= OPEN_FOUR) {
                responses[count++] = i;
            }
        }
        return count; /* 0 si pas de contre-attaque = victoire attaquant */
    }
    
    /* Si la menace est CLOSED_FOUR, une seule réponse : bloquer */
    if (threat_score >= CLOSED_FOUR) {
        int block = find_blocking_move(g, attacker);
        if (block != -1) {
            responses[count++] = block;
        }
        /* Aussi chercher le trou du gapped four */
        int gapped = find_gapped_four_hole(g, attacker);
        if (gapped != -1 && gapped != block) {
            responses[count++] = gapped;
        }
        return count;
    }
    
    /* Si la menace est OPEN_THREE, deux réponses possibles (bloquer chaque bout) */
    if (threat_score >= OPEN_THREE) {
        int blocking_moves[10];
        int block_count = find_line_blocking_moves(g, attacker, blocking_moves, 10);
        
        for (int i = 0; i < block_count && count < max_responses; i++) {
            responses[count++] = blocking_moves[i];
        }
        
        /* Aussi le trou du gapped three */
        int gapped = find_gapped_three_hole(g, attacker);
        if (gapped != -1) {
            bool already_in = false;
            for (int i = 0; i < count; i++) {
                if (responses[i] == gapped) { already_in = true; break; }
            }
            if (!already_in && count < max_responses) {
                responses[count++] = gapped;
            }
        }
        return count;
    }
    
    /* Menace faible (CLOSED_THREE) : plusieurs réponses possibles */
    int blocking_moves[10];
    int block_count = find_line_blocking_moves(g, attacker, blocking_moves, 10);
    
    for (int i = 0; i < block_count && count < max_responses; i++) {
        responses[count++] = blocking_moves[i];
    }
    
    return count;
}

/* ============================================================================
 * TSS RÉCURSIF - CŒUR DE L'ALGORITHME
 * 
 * Retourne :
 *   TSS_WIN     (1)  : Séquence gagnante trouvée
 *   TSS_UNKNOWN (0)  : Pas de conclusion
 *   TSS_LOSS    (-1) : L'adversaire peut se défendre
 * ============================================================================ */

static int tss_recursive(game *g, int attacker, int depth, clock_t deadline, int *best_move) {
    /* Vérifier timeout */
    if (clock() > deadline) return TSS_UNKNOWN;
    
    /* Profondeur max atteinte */
    if (depth <= 0) return TSS_UNKNOWN;
    
    int defender = (attacker == P1) ? P2 : P1;
    
    /* Générer les coups de menace (>= CLOSED_THREE) */
    ThreatMove threats[32];
    int threat_count = generate_threat_moves(g, attacker, threats, 32, CLOSED_THREE);
    
    /* Pas de menace = on ne peut pas gagner via TSS */
    if (threat_count == 0) return TSS_UNKNOWN;
    
    /* Limiter le nombre de menaces explorées (beam search) */
    int max_explore = (depth >= 6) ? 5 : 8;
    if (threat_count > max_explore) threat_count = max_explore;
    
    for (int i = 0; i < threat_count; i++) {
        MoveUndo undo;
        apply_move(g, threats[i].index, attacker, &undo);
        
        /* Vérifier victoire immédiate */
        if (threats[i].score >= WIN_SCORE || g->captures[attacker] >= 5) {
            undo_move(g, attacker, &undo);
            *best_move = threats[i].index;
            return TSS_WIN;
        }
        
        /* Trouver les réponses forcées de l'adversaire */
        int responses[8];
        int resp_count = find_forced_responses(g, defender, threats[i].index, threats[i].score, responses, 8);
        
        /* Pas de réponse = double menace créée ! */
        if (resp_count == 0) {
            undo_move(g, attacker, &undo);
            *best_move = threats[i].index;
            return TSS_WIN;
        }
        
        /* Explorer chaque réponse */
        bool all_responses_lead_to_win = true;
        
        for (int r = 0; r < resp_count && all_responses_lead_to_win; r++) {
            MoveUndo resp_undo;
            apply_move(g, responses[r], defender, &resp_undo);
            
            int dummy_move = -1;
            int result = tss_recursive(g, attacker, depth - 2, deadline, &dummy_move);
            
            undo_move(g, defender, &resp_undo);
            
            if (result != TSS_WIN) {
                all_responses_lead_to_win = false;
            }
        }
        
        undo_move(g, attacker, &undo);
        
        /* Si toutes les réponses mènent à une victoire, ce coup gagne */
        if (all_responses_lead_to_win) {
            *best_move = threats[i].index;
            return TSS_WIN;
        }
    }
    
    return TSS_UNKNOWN;
}

/* ============================================================================
 * FONCTIONS PUBLIQUES
 * ============================================================================ */

/* TSS Offensif : cherche si le joueur a une séquence gagnante */
int tss_find_winning_sequence(game *g, int player, clock_t start_time, int time_budget_ms) {
    clock_t deadline = start_time + (time_budget_ms * CLOCKS_PER_SEC / 1000);
    int best_move = -1;
    
    int result = tss_recursive(g, player, TSS_MAX_DEPTH, deadline, &best_move);
    
    if (result == TSS_WIN && best_move != -1) {
        #ifdef DEBUG
        printf("TSS: Séquence gagnante trouvée ! Coup: (%d, %d)\n", 
               GET_X(best_move), GET_Y(best_move));
        #endif
        return best_move;
    }
    
    return -1;
}

/* TSS Défensif : cherche si l'adversaire a une séquence gagnante */
int tss_find_threat_to_block(game *g, int ia_player, clock_t start_time, int time_budget_ms) {
    int opponent = (ia_player == P1) ? P2 : P1;
    clock_t deadline = start_time + (time_budget_ms * CLOCKS_PER_SEC / 1000);
    int threat_move = -1;
    
    int result = tss_recursive(g, opponent, TSS_MAX_DEPTH, deadline, &threat_move);
    
    if (result == TSS_WIN && threat_move != -1) {
        #ifdef DEBUG
        printf("TSS: Séquence gagnante adverse détectée ! Premier coup: (%d, %d)\n", 
               GET_X(threat_move), GET_Y(threat_move));
        #endif
        
        /* L'adversaire jouerait ici - on doit l'en empêcher */
        /* Option 1: Jouer nous-mêmes à cette case */
        if (g->board[threat_move] == EMPTY) {
            /* Vérifier si ce coup est bon pour nous aussi */
            g->board[threat_move] = ia_player;
            int our_score = get_point_score(g, GET_X(threat_move), GET_Y(threat_move), ia_player);
            g->board[threat_move] = EMPTY;
            
            if (our_score >= CLOSED_THREE) {
                return threat_move; /* Coup mixte idéal ! */
            }
        }
        
        /* Option 2: Chercher un autre coup qui interrompt la séquence */
        /* Pour l'instant, on bloque simplement le premier coup */
        return threat_move;
    }
    
    return -1;
}

/*
 * Helper : Vérifie si une case a des voisins (pierres dans un rayon de 2)
 */
static bool has_neighbors_for_threat(game *g, int idx) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] != EMPTY) {
                return true;
            }
        }
    }
    return false;
}

/*
 * FONCTION CORRIGÉE : Détecte si l'adversaire peut créer une double menace
 * au PROCHAIN coup (pas immédiatement, mais en 1 coup)
 * 
 * CORRECTION : Ne considère que les cases avec des voisins
 */
int detect_pre_double_threat(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    /* D'abord, compter les menaces AVANT tout coup adverse */
    int baseline_wins = 0;
    for (int j = 0; j < MAX_BOARD; j++) {
        if (g->board[j] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, j, opponent);
        if (score >= WIN_SCORE) baseline_wins++;
    }
    
    /* Si l'adversaire a déjà 1+ victoires, on est déjà en danger ! */
    if (baseline_wins >= 1) {
        return find_winning_move(g, opponent);
    }
    
    /* Pour chaque case vide PERTINENTE, simuler le coup adverse */
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* Ignorer les cases isolées */
        if (!has_neighbors_for_threat(g, i)) continue;
        
        /* Simuler le coup adverse */
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        /* NOUVEAU : Compter les VICTOIRES après ce coup (pas juste CLOSED_FOUR) */
        int wins_after = 0;
        int win_positions[5];
        
        for (int j = 0; j < MAX_BOARD && wins_after < 5; j++) {
            if (g->board[j] != EMPTY) continue;
            
            int score = evaluate_move_with_captures_full(g, j, opponent);
            if (score >= WIN_SCORE) {
                win_positions[wins_after++] = j;
            }
        }
        
        undo_move(g, opponent, &undo);
        
        /* Si l'adversaire aurait 2+ VICTOIRES après ce coup = PERDU ! */
        if (wins_after >= 2) {
            #ifdef DEBUG
            printf("ALERTE CRITIQUE: Coup en (%d,%d) crée DOUBLE VICTOIRE !\n",
                   GET_X(i), GET_Y(i));
            #endif
            return i;
        }
        
        /* Compter aussi les CLOSED_FOUR (menaces de victoire) */
        int threat_count = 0;
        int threat_positions[10];
        
        apply_move(g, i, opponent, &undo);
        
        for (int j = 0; j < MAX_BOARD && threat_count < 10; j++) {
            if (g->board[j] != EMPTY) continue;
            
            int score = evaluate_move_with_captures_full(g, j, opponent);
            if (score >= CLOSED_FOUR) {
                int dist_x = abs(GET_X(j) - GET_X(i));
                int dist_y = abs(GET_Y(j) - GET_Y(i));
                
                if (dist_x <= 5 && dist_y <= 5) {
                    threat_positions[threat_count++] = j;
                }
            }
        }
        
        undo_move(g, opponent, &undo);
        
        /* Vérifier les lignes différentes */
        if (threat_count >= 2) {
            bool different_lines = false;
            
            for (int t1 = 0; t1 < threat_count && !different_lines; t1++) {
                for (int t2 = t1 + 1; t2 < threat_count; t2++) {
                    int x1 = GET_X(threat_positions[t1]);
                    int y1 = GET_Y(threat_positions[t1]);
                    int x2 = GET_X(threat_positions[t2]);
                    int y2 = GET_Y(threat_positions[t2]);
                    
                    bool same_h = (y1 == y2);
                    bool same_v = (x1 == x2);
                    bool same_d1 = (x1 - y1 == x2 - y2);
                    bool same_d2 = (x1 + y1 == x2 + y2);
                    
                    if (!same_h && !same_v && !same_d1 && !same_d2) {
                        different_lines = true;
                        break;
                    }
                }
            }
            
            if (different_lines) {
                #ifdef DEBUG
                printf("ALERTE: Pré-double menace ! Adversaire joue en (%d,%d) → %d menaces\n",
                       GET_X(i), GET_Y(i), threat_count);
                #endif
                return i;
            }
        }
    }
    
    return -1;
}

/*
 * NOUVELLE FONCTION : Détecte si une capture adverse créerait DEUX CLOSED_FOUR connectés
 * C'est exactement le pattern exploité par le joueur
 * 
 * Retourne l'index de la case à bloquer (soit la capture, soit une des lignes)
 */
int detect_capture_creates_double_closed_four(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* L'adversaire peut-il capturer ici ? */
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (caps < 2) continue; /* Pas de capture possible */
        
        /* Simuler la capture */
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        /* Compter les CLOSED_FOUR créés après la capture */
        int closed_four_count = 0;
        int closed_four_positions[10];
        int cf_idx = 0;
        
        /* Scanner toutes les cases pour trouver des CLOSED_FOUR potentiels */
        for (int j = 0; j < MAX_BOARD && cf_idx < 10; j++) {
            if (g->board[j] != EMPTY) continue;
            
            int score = evaluate_move_with_captures_full(g, j, opponent);
            if (score >= CLOSED_FOUR && score < WIN_SCORE) {
                closed_four_positions[cf_idx++] = j;
                closed_four_count++;
            }
        }
        
        undo_move(g, opponent, &undo);
        
        /* DOUBLE CLOSED FOUR DÉTECTÉ ! */
        if (closed_four_count >= 2) {
            /* Vérifier si les deux CLOSED_FOUR sont sur des lignes différentes */
            if (cf_idx >= 2) {
                int x1 = GET_X(closed_four_positions[0]);
                int y1 = GET_Y(closed_four_positions[0]);
                int x2 = GET_X(closed_four_positions[1]);
                int y2 = GET_Y(closed_four_positions[1]);
                
                /* Si pas sur la même ligne → Double menace imparable ! */
                bool same_line = (x1 == x2) || (y1 == y2) || 
                                 (abs(x1 - x2) == abs(y1 - y2));
                
                if (!same_line) {
                    #ifdef DEBUG
                    printf("ALERTE CRITIQUE: Capture en (%d,%d) crée DOUBLE CLOSED FOUR !\n",
                           GET_X(i), GET_Y(i));
                    printf("  → CLOSED_FOUR 1 en (%d,%d)\n", x1, y1);
                    printf("  → CLOSED_FOUR 2 en (%d,%d)\n", x2, y2);
                    #endif
                    return i; /* Bloquer la case de capture */
                }
            }
        }
    }
    
    return -1;
}

/*
 * NOUVELLE FONCTION : Détecte si une capture adverse CONNECTERAIT deux segments
 * C'est exactement ce que le joueur exploite
 */
int detect_capture_connects_segments(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        /* L'adversaire peut-il capturer ici ? */
        g->board[i] = opponent;
        int caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        if (caps < 2) continue;
        
        /* Simuler la capture */
        MoveUndo undo;
        apply_move(g, i, opponent, &undo);
        
        /* Vérifier si la capture a CONNECTÉ des segments adverses */
        bool dangerous_connection = false;
        
        for (int c = 0; c < undo.captured_count && !dangerous_connection; c++) {
            int cap_idx = undo.captured_indices[c];
            int cx = GET_X(cap_idx);
            int cy = GET_Y(cap_idx);
            
            int dx[] = {1, 0, 1, 1};
            int dy[] = {0, 1, 1, -1};
            
            for (int d = 0; d < 4; d++) {
                int stones_pos = 0, stones_neg = 0;
                
                for (int k = 1; k <= 4; k++) {
                    int nx = cx + dx[d] * k;
                    int ny = cy + dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    if (g->board[GET_INDEX(nx, ny)] == opponent) stones_pos++;
                    else if (g->board[GET_INDEX(nx, ny)] != EMPTY) break;
                }
                
                for (int k = 1; k <= 4; k++) {
                    int nx = cx - dx[d] * k;
                    int ny = cy - dy[d] * k;
                    if (!IS_VALID(nx, ny)) break;
                    if (g->board[GET_INDEX(nx, ny)] == opponent) stones_neg++;
                    else if (g->board[GET_INDEX(nx, ny)] != EMPTY) break;
                }
                
                /* Connexion dangereuse : 2+ pierres de chaque côté */
                if (stones_pos >= 2 && stones_neg >= 2) {
                    dangerous_connection = true;
                    break;
                }
                /* Ou total >= 4 avec au moins 1 de chaque côté */
                if (stones_pos >= 1 && stones_neg >= 1 && 
                    stones_pos + stones_neg >= 4) {
                    dangerous_connection = true;
                    break;
                }
            }
        }
        
        undo_move(g, opponent, &undo);
        
        if (dangerous_connection) {
            #ifdef DEBUG
            printf("ALERTE: Capture adverse en (%d,%d) CONNECTE des segments !\n",
                   GET_X(i), GET_Y(i));
            #endif
            return i;
        }
    }
    
    return -1;
}
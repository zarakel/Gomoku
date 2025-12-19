#include "../include/gomoku.h"
#include <limits.h>

// Déclarations des fonctions (définies dans heuristics.c)
extern int find_gapped_four_hole(game *g, int player);
extern int find_gapped_three_hole(game *g, int player);

// NOUVELLE FONCTION : Vérifie si un alignement est "capturable" (contient des paires vulnérables)
static bool is_alignment_vulnerable(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int vulnerable = count_vulnerable_pairs(g, player);
    
    // Si on a des paires vulnérables ET l'adversaire peut capturer, nos alignements sont en danger
    if (vulnerable > 0) {
        int opp_capture = find_capture_move(g, opponent);
        if (opp_capture != -1) {
            return true;
        }
    }
    return false;
}

// NOUVELLE FONCTION : Calcule un "score de danger capture" pour un joueur
// Plus c'est élevé, plus le joueur est proche de gagner par captures
static int get_capture_danger_score(game *g, int player) {
    int captures = g->captures[player];
    int capture_move = find_capture_move(g, player);
    int can_capture = 0;
    
    if (capture_move != -1) {
        g->board[capture_move] = player;
        can_capture = count_potential_captures(g, GET_X(capture_move), GET_Y(capture_move), player) / 2;
        g->board[capture_move] = EMPTY;
    }
    
    int future_total = captures + can_capture;
    
    // Score exponentiel basé sur la proximité de la victoire
    if (future_total >= 5) return 10000000; // Victoire imminente
    if (future_total >= 4) return 5000000;  // Très dangereux
    if (future_total >= 3) return 2000000;  // Dangereux
    if (future_total >= 2) return 500000;   // À surveiller
    if (future_total >= 1) return 100000;   // Début de menace
    return 0;
}

// NOUVELLE FONCTION : Détecte si un joueur a une double menace (2+ lignes de 3+ pierres)
static int count_serious_threats(game *g, int player) {
    int threat_count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    bool counted[MAX_BOARD] = {false};
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int px = x - dx[d];
            int py = y - dy[d];
            if (IS_VALID(px, py) && g->board[GET_INDEX(px, py)] == player) continue;
            
            int stones = 1;
            int open_ends = 0;
            
            for (int k = 1; k < 6; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    open_ends++;
                    break;
                }
                else break;
            }
            
            int bx = x - dx[d];
            int by = y - dy[d];
            if (IS_VALID(bx, by) && g->board[GET_INDEX(bx, by)] == EMPTY) {
                open_ends++;
            }
            
            if (stones >= 3 && open_ends >= 1 && !counted[idx]) {
                threat_count++;
                counted[idx] = true;
            }
        }
    }
    return threat_count;
}

// NOUVELLE FONCTION : Compte les Gapped Threes d'un joueur (CRITIQUE)
static int count_gapped_threes(game *g, int player) {
    int count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    int opponent = (player == P1) ? P2 : P1;
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            // Buffer de 7 cases
            int line[7];
            for (int k = -3; k <= 3; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (IS_VALID(nx, ny)) {
                    line[k + 3] = g->board[GET_INDEX(nx, ny)];
                } else {
                    line[k + 3] = opponent;
                }
            }
            
            // Pattern: . X _ X X . (Open Gapped Three)
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == EMPTY &&
                    line[start + 3] == player &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
            
            // Pattern: . X X _ X . (Open Gapped Three)
            for (int start = 0; start <= 1; start++) {
                if (start + 5 >= 7) continue;
                if (line[start] == EMPTY &&
                    line[start + 1] == player &&
                    line[start + 2] == player &&
                    line[start + 3] == EMPTY &&
                    line[start + 4] == player &&
                    line[start + 5] == EMPTY) {
                    count++;
                }
            }
        }
    }
    return count;
}

// Trouve le meilleur coup "mixte" (attaque + défense)
static int find_best_dual_purpose_move(game *g, int ia_player, int opponent) {
    int best_idx = -1;
    int best_combined_score = 0;
    
    // D'abord, chercher s'il y a un Gapped Three adverse à bloquer
    int gapped_three = find_gapped_three_hole(g, opponent);
    if (gapped_three != -1 && g->board[gapped_three] == EMPTY) {
        // Ce coup bloque-t-il le gapped three ET crée-t-il une menace ?
        g->board[gapped_three] = ia_player;
        int attack_score = get_point_score(g, GET_X(gapped_three), GET_Y(gapped_three), ia_player);
        g->board[gapped_three] = EMPTY;
        
        if (attack_score >= CLOSED_THREE) {
            // Excellent ! On bloque ET on attaque
            return gapped_three;
        }
    }
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // Score offensif
        g->board[i] = ia_player;
        int attack_score = get_point_score(g, GET_X(i), GET_Y(i), ia_player);
        g->board[i] = EMPTY;
        
        // Score défensif
        g->board[i] = opponent;
        int defense_score = get_point_score(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        
        // NOUVEAU : Vérifier si ce coup bloque un Gapped Three
        int blocks_gapped = 0;
        if (i == gapped_three) {
            blocks_gapped = 5000000; // Gros bonus
        }
        
        int combined = 0;
        
        if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
            combined = attack_score + defense_score + 50000000 + blocks_gapped;
        }
        else if (attack_score >= CLOSED_THREE && defense_score >= CLOSED_THREE) {
            combined = attack_score + defense_score + 10000000 + blocks_gapped;
        }
        else {
            combined = attack_score + (defense_score / 2) + blocks_gapped;
        }
        
        if (combined > best_combined_score) {
            best_combined_score = combined;
            best_idx = i;
        }
    }
    
    return best_idx;
}

static int find_line_blocking_moves(game *g, int player, int *blocking_moves, int max_moves) {
    int count = 0;
    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};
    
    for (int idx = 0; idx < MAX_BOARD && count < max_moves; idx++) {
        if (g->board[idx] != player) continue;
        
        int x = GET_X(idx);
        int y = GET_Y(idx);
        
        for (int d = 0; d < 4; d++) {
            int stones = 1;
            int empty_before = -1;
            int empty_after = -1;
            
            for (int k = 1; k < 5; k++) {
                int nx = x + dx[d] * k;
                int ny = y + dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    empty_after = GET_INDEX(nx, ny);
                    break;
                }
                else break;
            }
            
            for (int k = 1; k < 5; k++) {
                int nx = x - dx[d] * k;
                int ny = y - dy[d] * k;
                if (!IS_VALID(nx, ny)) break;
                int cell = g->board[GET_INDEX(nx, ny)];
                if (cell == player) stones++;
                else if (cell == EMPTY) {
                    empty_before = GET_INDEX(nx, ny);
                    break;
                }
                else break;
            }
            
            if (stones >= 3) {
                if (empty_before != -1 && count < max_moves) {
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (blocking_moves[i] == empty_before) { found = true; break; }
                    }
                    if (!found) blocking_moves[count++] = empty_before;
                }
                if (empty_after != -1 && count < max_moves) {
                    bool found = false;
                    for (int i = 0; i < count; i++) {
                        if (blocking_moves[i] == empty_after) { found = true; break; }
                    }
                    if (!found) blocking_moves[count++] = empty_after;
                }
            }
        }
    }
    return count;
}

int vcf_search(game *g, int depth, int player, int ia_player, clock_t start_time) {
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return 0;
    if (abs(evaluate_board(g, ia_player)) > WIN_SCORE / 2) return 1; 
    if (depth == 0) return 0; 

    int opponent = (player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, depth, -1);
    
    MoveCandidate vcf_moves[MAX_BOARD];
    int vcf_count = 0;

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;
        if (player == ia_player) {
            g->board[idx] = player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), player) >= CLOSED_FOUR) 
                vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        } else {
            g->board[idx] = ia_player;
            if (get_point_score(g, GET_X(idx), GET_Y(idx), ia_player) >= CLOSED_FOUR) 
                vcf_moves[vcf_count++] = moves[i];
            g->board[idx] = EMPTY;
        }
    }

    if (player == ia_player && vcf_count == 0) return 0;
    if (player != ia_player && vcf_count == 0) return 1;

    for (int i = 0; i < vcf_count; i++) {
        MoveUndo undo;
        apply_move(g, vcf_moves[i].index, player, &undo);
        int result = vcf_search(g, depth - 1, opponent, ia_player, start_time);
        undo_move(g, player, &undo);
        if (player == ia_player && result == 1) return 1; 
        if (player != ia_player && result == 0) return 0;
    }
    return (player != ia_player); 
}

static int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    if (undo.captured_count > 0) {
        for (int i = 0; i < undo.captured_count; i++) {
            int cap_idx = undo.captured_indices[i];
            int cap_x = GET_X(cap_idx);
            int cap_y = GET_Y(cap_idx);
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = cap_x + dx;
                    int ny = cap_y + dy;
                    if (!IS_VALID(nx, ny)) continue;
                    int nidx = GET_INDEX(nx, ny);
                    if (g->board[nidx] == player) {
                        int new_score = get_point_score(g, nx, ny, player);
                        if (new_score > score) score = new_score;
                    }
                }
            }
        }
    }
    
    undo_move(g, player, &undo);
    return score;
}

static int find_winning_move(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, i, player);
        if (score >= WIN_SCORE) return i;
    }
    return -1;
}

static int find_blocking_move(game *g, int threat_player) {
    int ia_player = (threat_player == P1) ? P2 : P1;
    
    // ÉTAPE 0 : Chercher un GAPPED FOUR
    int gapped_hole = find_gapped_four_hole(g, threat_player);
    if (gapped_hole != -1 && g->board[gapped_hole] == EMPTY) {
        return gapped_hole;
    }
    
    // ÉTAPE 1 : Cases WIN directes
    int win_moves[10];
    int win_count = 0;
    
    for (int i = 0; i < MAX_BOARD && win_count < 10; i++) {
        if (g->board[i] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, i, threat_player);
        if (score >= WIN_SCORE) win_moves[win_count++] = i;
    }
    
    if (win_count == 1) return win_moves[0];
    if (win_count > 1) return win_moves[0];
    
    // ÉTAPE 1.5 : Gapped Three (PRIORITÉ HAUTE)
    int gapped_three = find_gapped_three_hole(g, threat_player);
    if (gapped_three != -1 && g->board[gapped_three] == EMPTY) {
        #ifdef DEBUG
        printf("[BLOCK] Gapped Three détecté ! Blocage du trou en (%d, %d)\n", 
               GET_X(gapped_three), GET_Y(gapped_three));
        #endif
        return gapped_three;
    }
    
    // ÉTAPE 2 : Extrémités des alignements
    int blocking_candidates[20];
    int block_count = find_line_blocking_moves(g, threat_player, blocking_candidates, 20);
    
    if (block_count > 0) {
        int best_block = blocking_candidates[0];
        int best_score = -1000000000;
        
        for (int i = 0; i < block_count; i++) {
            int idx = blocking_candidates[i];
            if (g->board[idx] != EMPTY) continue;
            
            int threat_before = 0;
            for (int j = 0; j < MAX_BOARD; j++) {
                if (g->board[j] != EMPTY) continue;
                int s = evaluate_move_with_captures_full(g, j, threat_player);
                if (s > threat_before) threat_before = s;
            }
            
            g->board[idx] = ia_player;
            
            int threat_after = 0;
            for (int j = 0; j < MAX_BOARD; j++) {
                if (g->board[j] != EMPTY) continue;
                int s = evaluate_move_with_captures_full(g, j, threat_player);
                if (s > threat_after) threat_after = s;
            }
            
            int our_attack = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
            
            g->board[idx] = EMPTY;
            
            int reduction = threat_before - threat_after;
            int combined_score = reduction + (our_attack / 2);
            
            if (our_attack >= OPEN_THREE) combined_score += 2000000;
            if (our_attack >= CLOSED_FOUR) combined_score += 10000000;
            
            if (combined_score > best_score) {
                best_score = combined_score;
                best_block = idx;
            }
        }
        
        return best_block;
    }
    
    return -1;
}

static void find_all_threats(game *g, int player, int *best_idx, int *best_score) {
    *best_idx = -1;
    *best_score = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        int score = evaluate_move_with_captures_full(g, i, player);
        if (score > *best_score) {
            *best_score = score;
            *best_idx = i;
        }
    }
}

int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;

    // ==========================================================
    // ÉTAPE 0 : VICTOIRE IMMÉDIATE (Alignement ou Capture)
    // ==========================================================
    int my_win = find_winning_move(g, ia_player);
    if (my_win != -1) return my_win;
    
    // Victoire par capture (5 paires)
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        g->board[i] = ia_player;
        int my_caps = count_potential_captures(g, GET_X(i), GET_Y(i), ia_player);
        g->board[i] = EMPTY;
        if (g->captures[ia_player] + my_caps / 2 >= 5) return i;
    }

    // ==========================================================
    // ÉTAPE 1 : BLOCAGE VICTOIRE ADVERSE (CRITIQUE)
    // ==========================================================
    int opp_win = find_winning_move(g, opponent);
    if (opp_win != -1) return opp_win;
    
    // Blocage victoire par capture adverse (PRIORITÉ ABSOLUE)
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        g->board[i] = opponent;
        int opp_caps = count_potential_captures(g, GET_X(i), GET_Y(i), opponent);
        g->board[i] = EMPTY;
        if (g->captures[opponent] + opp_caps / 2 >= 5) return i;
    }

    // ==========================================================
    // ÉTAPE 1.5 : ANALYSE COMPLÈTE DE LA SITUATION DE CAPTURE
    // ==========================================================
    int opp_capture_danger = get_capture_danger_score(g, opponent);
    int my_capture_score = get_capture_danger_score(g, ia_player);
    
    int opp_capture_move = find_capture_block_move(g, ia_player);
    int my_capture_move = find_capture_move(g, ia_player);
    int my_vulnerable = count_vulnerable_pairs(g, ia_player);
    
    // Calculer les captures potentielles
    int opp_can_capture = 0;
    if (opp_capture_move != -1) {
        g->board[opp_capture_move] = opponent;
        opp_can_capture = count_potential_captures(g, GET_X(opp_capture_move), GET_Y(opp_capture_move), opponent) / 2;
        g->board[opp_capture_move] = EMPTY;
    }
    
    int my_can_capture = 0;
    if (my_capture_move != -1) {
        g->board[my_capture_move] = ia_player;
        my_can_capture = count_potential_captures(g, GET_X(my_capture_move), GET_Y(my_capture_move), ia_player) / 2;
        g->board[my_capture_move] = EMPTY;
    }

    // ==========================================================
    // ÉTAPE 2 : DÉFENSE CAPTURE PRIORITAIRE
    // ==========================================================
    
    // CAS ULTRA-CRITIQUE : L'adversaire atteint 4+ paires
    if (opp_capture_move != -1 && g->captures[opponent] + opp_can_capture >= 4) {
        // On DOIT bloquer, sauf si on peut gagner immédiatement
        if (g->captures[ia_player] + my_can_capture >= 5) {
            return my_capture_move; // On gagne d'abord !
        }
        
        #ifdef DEBUG
        printf("[CAPTURE] BLOCAGE CRITIQUE ! Adversaire aurait %d paires.\n", 
               g->captures[opponent] + opp_can_capture);
        #endif
        return opp_capture_move;
    }
    
    // CAS CRITIQUE : L'adversaire a 3+ paires et peut en capturer
    if (opp_capture_move != -1 && g->captures[opponent] >= 3 && opp_can_capture >= 1) {
        // Contre-capturer SEULEMENT si ça nous fait atteindre 4+ paires
        if (my_capture_move != -1 && g->captures[ia_player] + my_can_capture >= 4) {
            #ifdef DEBUG
            printf("[CAPTURE] Contre-capture stratégique vers %d paires.\n", 
                   g->captures[ia_player] + my_can_capture);
            #endif
            return my_capture_move;
        }
        
        #ifdef DEBUG
        printf("[CAPTURE] Blocage défensif. Adversaire a %d paires.\n", g->captures[opponent]);
        #endif
        return opp_capture_move;
    }
    
    // CAS SÉRIEUX : L'adversaire a 2+ paires et peut en capturer ET nos alignements sont vulnérables
    if (opp_capture_move != -1 && g->captures[opponent] >= 2 && opp_can_capture >= 1) {
        // Vérifier si nos alignements sont menacés par les captures
        bool our_alignments_threatened = is_alignment_vulnerable(g, ia_player);
        
        if (our_alignments_threatened || my_vulnerable >= 2) {
            // Nos alignements ne valent rien s'ils peuvent être capturés !
            #ifdef DEBUG
            printf("[CAPTURE] Alignements menacés ! Défense prioritaire.\n");
            #endif
            return opp_capture_move;
        }
    }

    // ==========================================================
    // ÉTAPE 3 : SCAN DES MENACES D'ALIGNEMENT
    // ==========================================================
    int best_opp_idx = -1;
    int best_opp_score = 0;
    int my_best_idx = -1;
    int my_best_score = 0;
    
    find_all_threats(g, opponent, &best_opp_idx, &best_opp_score);
    find_all_threats(g, ia_player, &my_best_idx, &my_best_score);
    
    int opp_gapped_three = find_gapped_three_hole(g, opponent);
    int opp_gapped_count = count_gapped_threes(g, opponent);
    
    int my_threats = count_serious_threats(g, ia_player);
    int opp_threats = count_serious_threats(g, opponent);

    // ==========================================================
    // ÉTAPE 4 : DÉCISION AVEC PRISE EN COMPTE DES CAPTURES
    // ==========================================================
    
    // NOUVELLE LOGIQUE : Si l'adversaire a un avantage significatif en captures,
    // nos alignements offensifs perdent de la valeur car ils peuvent être détruits
    
    bool capture_mode_defense = (g->captures[opponent] >= 2 && opp_capture_move != -1);
    bool capture_mode_attack = (g->captures[ia_player] >= 2 && my_capture_move != -1);
    
    // Ajuster les scores effectifs basés sur la vulnérabilité
    int effective_my_score = my_best_score;
    int effective_opp_score = best_opp_score;
    
    if (capture_mode_defense && my_vulnerable > 0) {
        // Nos menaces sont moins dangereuses car l'adversaire peut les détruire
        effective_my_score = my_best_score / 2;
    }
    
    // CAS 1 : Adversaire a un CLOSED_FOUR ou mieux → BLOCAGE OBLIGATOIRE
    if (best_opp_score >= CLOSED_FOUR) {
        // Exception : On a un OPEN_FOUR (victoire garantie)
        if (my_best_score >= OPEN_FOUR) {
            return my_best_idx;
        }
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
        return best_opp_idx;
    }

    // CAS 2 : J'ai un OPEN_FOUR → VICTOIRE (sauf si vulnérable)
    if (my_best_score >= OPEN_FOUR) {
        // Vérifier que l'adversaire ne peut pas casser notre alignement par capture
        if (!capture_mode_defense || my_vulnerable == 0) {
            return my_best_idx;
        }
        // Sinon, la capture adverse est plus urgente
        if (opp_capture_move != -1) {
            return opp_capture_move;
        }
    }

    // CAS 3 : J'ai un CLOSED_FOUR → ATTAQUE (si pas trop vulnérable)
    if (my_best_score >= CLOSED_FOUR) {
        if (!capture_mode_defense || g->captures[opponent] < 3) {
            return my_best_idx;
        }
        // Adversaire a 3+ captures, attention aux contre-captures
        if (opp_capture_move != -1 && opp_can_capture >= 1) {
            return opp_capture_move;
        }
        return my_best_idx;
    }

    // CAS 3.5 : Adversaire a un Gapped Three → BLOCAGE PRIORITAIRE
    if (opp_gapped_three != -1 && opp_gapped_count > 0) {
        #ifdef DEBUG
        printf("[VCF] Gapped Three adverse détecté (%d patterns). Blocage en (%d, %d)\n", 
               opp_gapped_count, GET_X(opp_gapped_three), GET_Y(opp_gapped_three));
        #endif
        return opp_gapped_three;
    }

    // CAS 4 : Adversaire a un OPEN_THREE
    if (best_opp_score >= OPEN_THREE) {
        // Si on est en mode capture défensif, priorité à la défense capture
        if (capture_mode_defense && g->captures[opponent] >= 2) {
            if (opp_capture_move != -1) {
                #ifdef DEBUG
                printf("[VCF] Mode capture défensif actif. Blocage capture prioritaire.\n");
                #endif
                return opp_capture_move;
            }
        }
        
        if (effective_my_score >= OPEN_THREE) {
            int dual = find_best_dual_purpose_move(g, ia_player, opponent);
            if (dual != -1) {
                g->board[dual] = ia_player;
                int my_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), ia_player);
                g->board[dual] = EMPTY;
                
                g->board[dual] = opponent;
                int opp_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), opponent);
                g->board[dual] = EMPTY;
                
                if (my_score_after >= CLOSED_THREE && opp_score_after >= CLOSED_THREE) {
                    #ifdef DEBUG
                    printf("[VCF] Coup mixte trouvé : (%d, %d) Att:%d Def:%d\n", 
                           GET_X(dual), GET_Y(dual), my_score_after, opp_score_after);
                    #endif
                    return dual;
                }
            }
            
            // Attaquer seulement si on n'est pas en danger de capture
            if (my_threats > opp_threats && !capture_mode_defense) {
                #ifdef DEBUG
                printf("[VCF] Avantage offensif (%d vs %d menaces). Attaque !\n", my_threats, opp_threats);
                #endif
                return my_best_idx;
            }
        }
        
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
        return best_opp_idx;
    }
    
    // CAS 5 : J'ai un OPEN_THREE → DÉVELOPPER (si pas en danger)
    if (my_best_score >= OPEN_THREE) {
        // Vérifier qu'on n'est pas en danger de capture
        if (!capture_mode_defense || g->captures[opponent] < 2) {
            return my_best_idx;
        }
        // Sinon, défendre d'abord
        if (opp_capture_move != -1) {
            return opp_capture_move;
        }
        return my_best_idx;
    }
    
    // CAS 6 : Opportunité de capture offensive (si pas de menace critique)
    if (my_capture_move != -1 && g->captures[ia_player] >= 1) {
        if (best_opp_score < OPEN_THREE) {
            #ifdef DEBUG
            printf("[CAPTURE] Opportunité de capture en (%d, %d). Total après: %d paires\n", 
                   GET_X(my_capture_move), GET_Y(my_capture_move), g->captures[ia_player] + my_can_capture);
            #endif
            return my_capture_move;
        }
    }
    
    // CAS 7 : Adversaire a un CLOSED_THREE → Bloquer ou contre-attaquer
    if (best_opp_score >= CLOSED_THREE) {
        int dual = find_best_dual_purpose_move(g, ia_player, opponent);
        if (dual != -1) {
            g->board[dual] = ia_player;
            int my_score_after = get_point_score(g, GET_X(dual), GET_Y(dual), ia_player);
            g->board[dual] = EMPTY;
            
            if (my_score_after >= CLOSED_THREE) {
                return dual;
            }
        }
        
        int block = find_blocking_move(g, opponent);
        if (block != -1) return block;
    }
    
    // ==========================================================
    // ÉTAPE 8 : Pas de menace critique → MINIMAX
    // ==========================================================
    return -1;
}

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    debug_node_count++;

    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { if (entry->value > alpha) alpha = entry->value; }
        else if (entry->flag == TT_UPPERBOUND) { if (entry->value < beta) beta = entry->value; }
        if (alpha >= beta) {
            debug_cutoff_count++;
            return entry->value;
        }
    }
    
    if ((debug_node_count & 2047) == 0) {
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return -2;
    }

    int current_player = maximizingPlayer ? ia_player : ((ia_player == P1) ? P2 : P1);

    int current_eval = evaluate_board(g, ia_player);
    if (abs(current_eval) >= WIN_SCORE / 2) return current_eval;

    if (depth <= 0) return current_eval;

    MoveCandidate moves[MAX_BOARD];
    int tt_move = (entry != NULL) ? entry->best_move : -1; 

    int move_count = generate_moves(g, moves, current_player, depth, tt_move);

    if (move_count == 0) return current_eval;

    int best_val = maximizingPlayer ? (-WIN_SCORE - 1000) : (WIN_SCORE + 1000);
    int best_move_this_node = moves[0].index;

    for (int i = 0; i < move_count; i++) {
        int idx = moves[i].index;

        if (is_double_three(g, idx, current_player)) continue; 

        MoveUndo undo;
        apply_move(g, idx, current_player, &undo);
        
        int val;
        
        if (i == 0) {
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
        } else {
            int reduction = 0;
            if (depth >= 4 && i >= 4) reduction = 1;
            if (depth >= 6 && i >= 8) reduction = 2;
            
            if (maximizingPlayer) {
                val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, !maximizingPlayer, ia_player, start_time);
            } else {
                val = minimax(g, depth - 1 - reduction, beta - 1, beta, !maximizingPlayer, ia_player, start_time);
            }

            if (reduction > 0 && ((maximizingPlayer && val > alpha) || (!maximizingPlayer && val < beta))) {
                val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
        }

        undo_move(g, current_player, &undo);
        if (val == -2) return -2;

        if (maximizingPlayer) {
            if (val > best_val) { best_val = val; best_move_this_node = idx; }
            if (val >= beta) {
                debug_cutoff_count++;
                tt_save(g->current_hash, depth, val, TT_LOWERBOUND, best_move_this_node);
                return best_val;
            }
            if (val > alpha) alpha = val;
        } else {
            if (val < best_val) { best_val = val; best_move_this_node = idx; }
            if (val <= alpha) {
                debug_cutoff_count++;
                tt_save(g->current_hash, depth, val, TT_UPPERBOUND, best_move_this_node);
                return best_val;
            }
            if (val < beta) beta = val;
        }
    }

    tt_save(g->current_hash, depth, best_val, TT_EXACT, best_move_this_node);
    return best_val;
}
#include "../include/gomoku.h"
#include <limits.h>

static int run_aspiration_search(game *g, int depth, int prev_score, int *best_move_out, int ia_player, clock_t start) {
    int alpha = -WIN_SCORE - 10000;
    int beta = WIN_SCORE + 10000;
    int window = 100000;
    
    if (depth > 2 && abs(prev_score) < WIN_SCORE - 10000) {
        window = 100000 + (abs(prev_score) / 20);
        alpha = prev_score - window;
        beta = prev_score + window;
        if (alpha < -WIN_SCORE - 10000) alpha = -WIN_SCORE - 10000;
        if (beta > WIN_SCORE + 10000) beta = WIN_SCORE + 10000;
    }

    int loop_guard = 0;
    while (loop_guard++ < 3) {
        int alpha_origin = alpha;
        int beta_origin = beta;
        int current_best_idx = -1;
        int current_best_score = INT_MIN;
        bool time_out = false;
        
        MoveCandidate moves[MAX_BOARD];
        int count = generate_moves(g, moves, ia_player, depth, *best_move_out);
        if (count == 0) return prev_score; 
        if (*best_move_out == -1) *best_move_out = moves[0].index;

        for (int i = 0; i < count; i++) {
            int idx = moves[i].index;
            MoveUndo undo;
            apply_move(g, idx, ia_player, &undo);
            int val = minimax(g, depth - 1, alpha, beta, false, ia_player, start);
            undo_move(g, ia_player, &undo);

            if (val == TIMEOUT_CODE) { time_out = true; break; }
            if (val > current_best_score) {
                current_best_score = val;
                current_best_idx = idx;
            }
            if (val > alpha) alpha = val;
            if (alpha >= beta) { debug_cutoff_count++; break; }
        }

        if (time_out) return TIMEOUT_CODE; 
        if (depth > 2 && (current_best_score < alpha_origin || current_best_score > beta_origin)) {
            alpha = -WIN_SCORE - 10000; 
            beta = WIN_SCORE + 10000;
            continue; 
        }
        *best_move_out = current_best_idx;
        return current_best_score;
    }
    return prev_score;
}

static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_best_move = -1;
    int prev_score = 0;
    double allocated_time = (double)TIME_LIMIT_MS / 1000.0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {
        if (best_move != -1) prev_best_move = best_move;
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ia_player, start);
        if (score == TIMEOUT_CODE) return (prev_best_move != -1) ? prev_best_move : best_move;

        prev_score = score;
        printf("Depth %d complete. Score: %d\n", depth, score);
        if (score > WIN_SCORE - 5000 || (double)(clock() - start) / CLOCKS_PER_SEC > allocated_time * 0.60) break;
    }
    return best_move;
}

static void finalize_move(game *g, screen *win, int move_idx, int player, clock_t start, bool is_vcf) {
    if (move_idx != -1) {
        int x = GET_X(move_idx);
        int y = GET_Y(move_idx);
        MoveUndo final_undo;
        apply_move(g, move_idx, player, &final_undo);
        drawSquare(win, x, y, player);
        if (final_undo.captured_count > 0) {
            for (int k = 0; k < final_undo.captured_count; k++) {
                int cap_idx = final_undo.captured_indices[k];
                drawSquare(win, GET_X(cap_idx), GET_Y(cap_idx), EMPTY);
            }
        }
        win->changed = true; 
        printf("IA plays at (%d, %d) [%.3fs]\n", x, y, (double)(clock() - start) / CLOCKS_PER_SEC);
        g->ia_timer.elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    }
}

void makeIaMove(game *gameData, screen *windows) {
    launchTimer(&gameData->ia_timer);
    clock_t start = clock();
    int ia_player = gameData->turn;
    int opponent = (ia_player == P1) ? P2 : P1;
    int best_move = -1;
    bool forcing_found = false;
    char *reason = "Minimax";

    refresh_board_stats(gameData);
    update_crisis_state(gameData, ia_player);

    // --- PHASE 1 : SCAN UNIFIÉ (Gagner ou Bloquer) ---
    int max_opp_threat = 0;
    int threat_idx = -1;
    int my_win_idx = -1;

    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (gameData->board[idx] != EMPTY) continue;

        // Test de MA victoire
        gameData->board[idx] = ia_player;
        int my_s = get_point_score(gameData, GET_X(idx), GET_Y(idx), ia_player);
        int my_c = count_potential_captures(gameData, GET_X(idx), GET_Y(idx), ia_player);
        gameData->board[idx] = EMPTY;
        if ((my_s >= WIN_SCORE || gameData->captures[ia_player] + (my_c/2) >= 5) && !is_double_three(gameData, idx, ia_player)) {
            my_win_idx = idx; break;
        }

        // Test menace ADVERSE
        gameData->board[idx] = opponent;
        int opp_s = get_point_score(gameData, GET_X(idx), GET_Y(idx), opponent);
        int opp_c = count_potential_captures(gameData, GET_X(idx), GET_Y(idx), opponent);
        gameData->board[idx] = EMPTY;
        if (gameData->captures[opponent] + (opp_c/2) >= 5) opp_s = WIN_SCORE;
        if (opp_s > max_opp_threat) { max_opp_threat = opp_s; threat_idx = idx; }
    }

    if (my_win_idx != -1) {
        best_move = my_win_idx; reason = "Victoire immédiate"; forcing_found = true;
    } else if (max_opp_threat >= CLOSED_FOUR) {
        if (threat_idx != -1 && !is_double_three(gameData, threat_idx, ia_player)) {
            best_move = threat_idx; reason = "Blocage menace critique"; forcing_found = true;
        }
    }

    // --- PHASE 2 : VCF & MINIMAX ---
    if (!forcing_found) {
        int vcf_move = find_winning_vcf(gameData, ia_player);
        if (vcf_move != -1 && !is_double_three(gameData, vcf_move, ia_player)) {
            best_move = vcf_move; reason = "VCF Gagnant";
        } else {
            best_move = run_iterative_deepening(gameData, ia_player, start);
            // Sécurité anti-abandon
            if (evaluate_board(gameData, ia_player) < -WIN_SCORE / 2 && threat_idx != -1) {
                if (!is_double_three(gameData, threat_idx, ia_player)) {
                    best_move = threat_idx; reason = "Défense de dernier recours";
                }
            }
        }
    }

    double time_spent = (double)(clock() - start) / CLOCKS_PER_SEC;

    if (best_move != -1) {
        printf("✅ [%s] (%d, %d). Raison: %s [Temps: %.3fs]\n", 
               forcing_found ? "FORCED" : "AI", 
               GET_X(best_move), GET_Y(best_move), 
               reason, 
               time_spent);
    }

    // --- VALIDATION & SECOURS ---
    if (best_move == -1 || is_double_three(gameData, best_move, ia_player)) {
        for (int i = 0; i < MAX_BOARD; i++) {
            if (gameData->board[i] == EMPTY && !is_double_three(gameData, i, ia_player)) {
                best_move = i; break;
            }
        }
    }

    printf("✅ [%s] (%d, %d). Raison: %s\n", forcing_found ? "FORCED" : "AI", GET_X(best_move), GET_Y(best_move), reason);
    finalize_move(gameData, windows, best_move, ia_player, start, false);
}
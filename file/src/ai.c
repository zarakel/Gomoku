#include "../include/gomoku.h" // Assurez-vous d'avoir le prototype de is_double_three
#include <limits.h>
#include <string.h> // Pour memset
#include <stdlib.h> // Pour rand, abs

// Structure interne pour trier les coups
typedef struct {
    int index;
    int score_estim;
} MoveCandidate;

// --- GLOBALES ZOBRIST & TT ---

uint64_t zobrist_table[MAX_BOARD][3]; // [Case][Joueur (Vide, P1, P2)]
TTEntry transposition_table[TT_SIZE];

// Générateur de nombres aléatoires 64 bits
uint64_t rand64() {
    return (uint64_t)rand() ^ ((uint64_t)rand() << 15) ^ 
           ((uint64_t)rand() << 30) ^ ((uint64_t)rand() << 45) ^ 
           ((uint64_t)rand() << 60);
}

// À APPELER UNE FOIS DANS LE MAIN AU DÉBUT DU PROGRAMME
void init_zobrist() {
    for (int i = 0; i < MAX_BOARD; i++) {
        zobrist_table[i][0] = rand64(); // Pas vraiment utilisé pour vide, mais sécu
        zobrist_table[i][P1] = rand64();
        zobrist_table[i][P2] = rand64();
    }
    // Initialiser la TT à 0
    memset(transposition_table, 0, sizeof(transposition_table));
}

// Fonction pour calculer le hash complet (au début ou debug)
uint64_t compute_hash(game *g) {
    uint64_t h = 0;
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            h ^= zobrist_table[i][g->board[i]];
        }
    }
    return h;
}

// Killer Heuristic : [Depth][Slot]. On garde 2 coups tueurs par profondeur.
int killer_moves[MAX_DEPTH][2];

// History Heuristic : [BoardIndex]. Score cumulatif.
int history_heuristic[MAX_BOARD];

// Pour le debug des cutoffs
long long debug_node_count = 0;
long long debug_cutoff_count = 0;

// Reset des heuristiques au début d'un tour complet de l'IA
void clear_heuristics() {
    memset(killer_moves, -1, sizeof(killer_moves));
    memset(history_heuristic, 0, sizeof(history_heuristic));
    debug_node_count = 0;
    debug_cutoff_count = 0;
}

// --- GESTION DE LA TRANSPOSITION TABLE ---

void tt_save(uint64_t key, int depth, int val, int flag, int best_move) {
    int idx = key % TT_SIZE; 
    
    // Stratégie de remplacement : 
    // On remplace si la nouvelle entrée est plus profonde ou si c'est une collision ancienne
    if (transposition_table[idx].key != key || depth >= transposition_table[idx].depth) {
        transposition_table[idx].key = key;
        transposition_table[idx].depth = depth;
        transposition_table[idx].value = val;
        transposition_table[idx].flag = flag;
        transposition_table[idx].best_move = best_move;
    }
}

TTEntry* tt_probe(uint64_t key) {
    int idx = key % TT_SIZE;
    if (transposition_table[idx].key == key) {
        return &transposition_table[idx];
    }
    return NULL;
}

// --- FONCTIONS UTILITAIRES ---

/* Vérifie si une case a des voisins (rayon 2) non vides. */
bool has_neighbors(game *g, int idx) {
    int cx = GET_X(idx);
    int cy = GET_Y(idx);
    int radius = 2; // Rayon de recherche

    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (!IS_VALID(x, y)) continue;
            if (g->board[GET_INDEX(x, y)] != EMPTY) return true;
        }
    }
    return false;
}

/* Compare pour le qsort (Tri des coups) */
int compare_moves(const void *a, const void *b) {
    MoveCandidate *m1 = (MoveCandidate *)a;
    MoveCandidate *m2 = (MoveCandidate *)b;
    // Tri décroissant
    return m2->score_estim - m1->score_estim;
}

/* Évaluation rapide d'un coup unique pour le tri (Move Ordering). */
int quick_evaluate_move(game *g, int idx, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx);
    int y = GET_Y(idx);

    // 1. CALCUL DÉFENSE
    g->board[idx] = opponent;
    int defense_score = get_point_score(g, x, y, opponent);
    g->board[idx] = EMPTY;

    // 2. CALCUL ATTAQUE
    // On le calcule maintenant systématiquement pour ne pas rater les combos
    g->board[idx] = player; 
    int attack_score = get_point_score(g, x, y, player);
    g->board[idx] = EMPTY;

    // --- 3. DÉTECTION DES "COUPS D'INTERSECTION" (GOD MOVES) ---
    
    // CAS ULTIME (Votre cas) : Je crée un Open 4 (Gagnant) ET je bloque une menace (Open 3 ou +)
    // attack_score >= 20M (Open 4) et defense_score >= 50k (Closed 3 / Open 3)
    if (attack_score >= 15000000 && defense_score >= 50000) {
        return 2147483600; // PRIORITÉ ABSOLUE (Max Int - epsilon)
    }

    // CAS FORT : Je crée un Open 3 ET je bloque un Open 3
    // C'est un pivot majeur.
    if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
        return 2100000000; 
    }

    // --- 4. PRIORITÉS CLASSIQUES (Si pas de combo) ---

    // Victoire immédiate (5 alignés)
    if (attack_score >= WIN_SCORE) return 2147483647; 

    // Défense de mort immédiate (Bloquer 5 ou Open 4)
    if (defense_score >= WIN_SCORE) return 2000000000; 
    if (defense_score >= OPEN_FOUR) return 1500000000; 

    // Bloquer un Closed Four (Force la réponse)
    if (defense_score >= 200000) return 1200000000;

    // Bloquer un Open 3
    if (defense_score >= OPEN_THREE) return 1000000000; 

    // Créer un Open 4 (Attaque très forte)
    // Si on n'est pas menacé dans l'immédiat, c'est le meilleur coup
    if (attack_score >= 15000000) return 950000000;

    // Bloquer un Closed 3 / Broken 3
    if (defense_score >= 50000) return 900000000; 
    
    // --- 5. SCORE HYBRIDE POUR LE RESTE ---
    // Si le coup est bon offensivement (> 5000) ET défensivement (> 5000)
    int total = attack_score + defense_score;
    
    if (attack_score > 5000 && defense_score > 5000) {
        total += 50000; // Bonus "Move of Value"
    }

    return total;
}

/* Génère les coups et les trie (VERSION OPTIMISÉE SANS CHECK DOUBLE-THREE) */
int generate_moves(game *g, MoveCandidate *moves, int player, int depth, int tt_best_move) {
    int count = 0;
    int center = BOARD_SIZE / 2;

    // OPTIMISATION : BOUNDING BOX
    // Au lieu de parcourir 0..361, on cherche les limites du jeu actuel.
    int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;
    bool empty_board = true;

    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) {
            empty_board = false;
            int cx = GET_X(i);
            int cy = GET_Y(i);
            if (cx < min_x) min_x = cx;
            if (cx > max_x) max_x = cx;
            if (cy < min_y) min_y = cy;
            if (cy > max_y) max_y = cy;
        }
    }

    // Si plateau vide, on joue au centre
    if (empty_board) {
        int center_idx = GET_INDEX(center, center);
        moves[0].index = center_idx;
        moves[0].score_estim = 20000000;
        return 1;
    }

    // On élargit la box de 2 cases (rayon d'influence)
    min_x = (min_x - 2 < 0) ? 0 : min_x - 2;
    max_x = (max_x + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_x + 2;
    min_y = (min_y - 2 < 0) ? 0 : min_y - 2;
    max_y = (max_y + 2 >= BOARD_SIZE) ? BOARD_SIZE - 1 : max_y + 2;

    // On ne parcourt que le rectangle actif
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int i = GET_INDEX(x, y);

            if (g->board[i] != EMPTY) continue;
            if (!has_neighbors(g, i)) continue;

            // --- OPTIMISATION 1 : SUPPRESSION DU CHECK DOUBLE-THREE ICI ---
            // On ne vérifie plus is_double_three ici. C'est trop lent.
            // On le fera dans le minimax uniquement pour les coups retenus.
            
            moves[count].index = i;
            int score = 0;

            // 1. PV-MOVE / TT-MOVE
            if (i == tt_best_move) {
                score = 2000000000; 
            }
            // 2. KILLER MOVES
            else if (i == killer_moves[depth][0]) score = 100000000;
            else if (i == killer_moves[depth][1]) score = 90000000;
            // 3. TACTIQUE
            else {
                // quick_evaluate_move est rapide, on le garde
                int tactical = quick_evaluate_move(g, i, player);
                
                // --- OPTIMISATION 2 : MUST-PLAY CUTOFF ---
                // Si on trouve un coup qui sauve la partie (Bloquer Open 4 ou Win),
                // On le marque comme "Infini".
                if (tactical >= 1500000000) {
                    moves[0].index = i;
                    moves[0].score_estim = tactical;
                    return 1; // ON RETOURNE IMMÉDIATEMENT 1 SEUL COUP !
                }
                
                int history = history_heuristic[i];
                int dist_center = abs(x - center) + abs(y - center);
                score = tactical + history + (40 - dist_center);
            }

            moves[count].score_estim = score;
            count++;
        }
    }

    qsort(moves, count, sizeof(MoveCandidate), compare_moves);
    
    // --- BEAM SEARCH (LIMITATION DE LARGEUR INTELLIGENTE) ---
    
    // Au lieu de couper brutalement à 6, on garde TOUS les coups qui sont des menaces.
    // Seuil de menace : Open 3 (8000) ou Broken 4 (6000). Disons 5000 pour être large.
    
    int final_count = 0;
    int beam_width = 12; // Un peu plus large par défaut
    if (depth >= 6) beam_width = 8; // Plus strict en profondeur

    for (int i = 0; i < count; i++) {
        // On garde toujours les X premiers (Beam Width)
        if (i < beam_width) {
            final_count++;
        } 
        // MAIS on garde AUSSI les coups forts situés après la coupure !
        // Si le coup vaut > 4000 points (c'est une menace sérieuse type Open 3 ou défense forcée)
        // on le garde impérativement, même s'il est 15ème dans la liste.
        else if (moves[i].score_estim > 4000) {
            final_count++;
        }
        else {
            break; // Les coups suivants sont faibles et hors du beam, on arrête.
        }
    }

    return final_count;
}

/* Helper pour mettre à jour le score global incrémentalement */
void update_score_impact(game *g, int idx) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    
    int p1_before = get_point_score(g, x, y, P1);
    int p2_before = get_point_score(g, x, y, P2);

    g->score[P1] -= p1_before;
    g->score[P2] -= p2_before;
}

// --- LOGIQUE DO / UNDO ---

void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;

    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];

    // 1. Score Update
    update_score_impact(g, idx);
    
    // 2. Pose de pierre et UPDATE ZOBRIST
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player]; 
    
    g->score[P1] += get_point_score(g, x, y, P1);
    g->score[P2] += get_point_score(g, x, y, P2);

    // 3. Captures
    undo->captured_count = apply_captures_for_ai(g, x, y, player, undo->captured_indices);
    g->captures[player] += (undo->captured_count / 2);

    if (undo->captured_count > 0) {
        for (int i = 0; i < undo->captured_count; i++) {
            int cap_idx = undo->captured_indices[i];
            int cx = GET_X(cap_idx);
            int cy = GET_Y(cap_idx);
            
            // UPDATE ZOBRIST : RETRAIT ADVERSAIRE
            g->current_hash ^= zobrist_table[cap_idx][opponent];

            // Mise à jour scores
            g->board[cap_idx] = opponent; 
            g->score[P1] -= get_point_score(g, cx, cy, P1);
            g->score[P2] -= get_point_score(g, cx, cy, P2);
            g->board[cap_idx] = EMPTY; 
            g->score[P1] += get_point_score(g, cx, cy, P1);
            g->score[P2] += get_point_score(g, cx, cy, P2);
        }
    }
}

void undo_move(game *g, int player, MoveUndo *undo) {
    int idx = undo->move_idx;
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;

    // A. Annulation Captures
    if (undo->captured_count > 0) {
        for (int i = 0; i < undo->captured_count; i++) {
            int cap_idx = undo->captured_indices[i];
            int cx = GET_X(cap_idx);
            int cy = GET_Y(cap_idx);

            g->score[P1] -= get_point_score(g, cx, cy, P1);
            g->score[P2] -= get_point_score(g, cx, cy, P2);

            g->board[cap_idx] = opponent;
            
            // UPDATE ZOBRIST : ON REMET L'ADVERSAIRE
            g->current_hash ^= zobrist_table[cap_idx][opponent]; 

            g->score[P1] += get_point_score(g, cx, cy, P1);
            g->score[P2] += get_point_score(g, cx, cy, P2);
        }
    }

    // B. Annulation Coup Principal
    g->score[P1] -= get_point_score(g, x, y, P1);
    g->score[P2] -= get_point_score(g, x, y, P2);

    g->board[idx] = EMPTY;
    g->current_hash ^= zobrist_table[idx][player]; 

    g->score[P1] += get_point_score(g, x, y, P1);
    g->score[P2] += get_point_score(g, x, y, P2);

    g->captures[P1] = undo->prev_captures[P1];
    g->captures[P2] = undo->prev_captures[P2];
}

// --- MOTEUR ALPHA-BETA ---

int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, int ia_player, clock_t start_time) {
    debug_node_count++;

    // 1. TT PROBE
    int alpha_orig = alpha; 
    TTEntry *entry = tt_probe(g->current_hash);
    if (entry != NULL && entry->depth >= depth) {
        if (entry->flag == TT_EXACT) return entry->value;
        else if (entry->flag == TT_LOWERBOUND) { if (entry->value > alpha) alpha = entry->value; }
        else if (entry->flag == TT_UPPERBOUND) { if (entry->value < beta) beta = entry->value; }
        if (alpha >= beta) return entry->value;
    }
    
    // Check Temps (OPTIMISATION : On vérifie 2x plus souvent, tous les 2048 noeuds)
    // Le masque passe de 4095 à 2047.
    if ((debug_node_count & 2047) == 0) {
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return -2;
    }

    int current_eval = evaluate_board(g, ia_player);
    if (abs(current_eval) > WIN_SCORE / 2) return current_eval;

    // --- OPTIMISATION 1 : DELTA PRUNING (NOUVEAU) ---
    // Si on est en phase de capture (Quiescence) ou faible profondeur,
    // et que notre score est très loin d'Alpha, on abandonne.
    // Marge de sécurité : 10000 (Un peu plus qu'un Open 3)
    if (depth <= 1 && !maximizingPlayer) {
        int margin = 10000;
        if (current_eval + margin < alpha) return alpha; 
    }
    // -------------------------------------------------

    // --- OPTIMISATION 2 : FUTILITY PRUNING ---
    if (depth == 1 && !maximizingPlayer) {
        if (current_eval + 9000 < alpha) return current_eval;
    }

    // VCF Gatekeeper
    if (depth == 0) {
        if (maximizingPlayer && current_eval >= 6000) {
             // CORRECTION : On passe start_time au VCF
             if (vcf_search(g, 6, ia_player, ia_player, start_time)) return WIN_SCORE - 100; 
        }
        return current_eval;
    }

    MoveCandidate moves[MAX_BOARD];
    int current_player = maximizingPlayer ? ia_player : ((ia_player == P1) ? P2 : P1);
    int tt_move = (entry != NULL) ? entry->best_move : -1; 
    
    int move_count = generate_moves(g, moves, current_player, depth, tt_move);
    if (move_count == 0) return 0;

    int best_val = maximizingPlayer ? INT_MIN : INT_MAX;
    int best_move_this_node = -1; // <--- DÉCLARATION ICI (Initialisation à -1)
    int valid_moves_played = 0; 

    for (int i = 0; i < move_count; i++) {
        int idx = moves[i].index;

        // --- LAZY VALIDATION & LOGGING ---
        
        // 1. Check Double-Three
        if (is_double_three(g, idx, current_player)) {
            // 2. Check Capture (Seule exception autorisée)
            int capture_indices[10];
            // Note: On suppose que vous avez une version de apply_captures qui ne modifie pas ou qu'on restaure
            // Ici on utilise une version hypothétique "check_captures" ou on fait/undo
            int caps = apply_captures_for_ai(g, GET_X(idx), GET_Y(idx), current_player, capture_indices);
            
            // Restauration immédiate pour ne pas casser le plateau
            int opponent = (current_player == P1) ? P2 : P1;
            for(int k=0; k<caps; k++) g->board[capture_indices[k]] = opponent;
            
            if (caps == 0) {
                // LOGGING : Seulement si on est à la racine (le "vrai" coup que l'IA envisage)
                // On suppose que la profondeur initiale est passée ou stockée, sinon on peut utiliser une macro DEBUG
                #ifdef DEBUG
                // Si c'est un coup très bien noté (premier de la liste), ça vaut le coup de dire pourquoi on le jette
                if (i == 0 && depth > 4) { 
                    printf("[IA] Coup (%d, %d) rejeté : Double-Three interdit.\n", GET_X(idx), GET_Y(idx));
                    explain_double_three(g, idx, current_player);
                }
                #endif
                continue; // COUP ILLÉGAL
            }
        }
        // ---------------------------------

        MoveUndo undo;
        apply_move(g, idx, current_player, &undo);
        valid_moves_played++;
        
        int val;

        // --- OPTIMISATION 3 : LMR EXTRÊME ---
        if (i == 0) {
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
        } 
        else {
            int reduction = 0;
            
            if (depth >= 3) {
                // Réduction progressive mais brutale
                if (i >= 2) reduction = 1;
                if (i >= 4) reduction = 2;
                if (i >= 8) reduction = 3;
                if (depth >= 8 && i >= 12) reduction = 4; // On massacre la profondeur ici
                
                // Sécurité : on ne descend pas sous 1
                if (depth - 1 - reduction < 1) reduction = depth - 2;
                if (reduction < 0) reduction = 0;
            }

            // Recherche réduite (Null Window)
            if (maximizingPlayer) {
                val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, !maximizingPlayer, ia_player, start_time);
            } else {
                val = minimax(g, depth - 1 - reduction, beta - 1, beta, !maximizingPlayer, ia_player, start_time);
            }

            // Re-search si échec LMR (Le coup était meilleur que prévu)
            if (reduction > 0) {
                if ((maximizingPlayer && val > alpha) || (!maximizingPlayer && val < beta)) {
                     val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
                }
            }
            
            // Re-search PVS classique si besoin (Fail High)
            if (maximizingPlayer && val > alpha && val < beta && reduction == 0) {
                 val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
            if (!maximizingPlayer && val < beta && val > alpha && reduction == 0) {
                 val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ia_player, start_time);
            }
        }
        // ---------------------------------------------

        undo_move(g, current_player, &undo);

        if (val == -2) return -2; 

        if (maximizingPlayer) {
            if (val > best_val) { 
                best_val = val; 
                best_move_this_node = idx; // <--- MISE À JOUR ICI (On a trouvé mieux)
            }            
            if (val > alpha) alpha = val;
            if (beta <= alpha) {
                best_move_this_node = idx; // <--- MISE À JOUR ICI (Coup qui cause le cutoff)
                // Mise à jour Killers & History
                if (killer_moves[depth][0] != idx) {
                    killer_moves[depth][1] = killer_moves[depth][0];
                    killer_moves[depth][0] = idx;
                }
                history_heuristic[idx] += (depth * depth);
                debug_cutoff_count++;
                break;
            }
        } else {
            if (val < best_val) { 
                best_val = val; 
                best_move_this_node = idx; // <--- MISE À JOUR ICI
            }
            if (val < beta) beta = val;
            if (beta <= alpha) {
                best_move_this_node = idx; // <--- MISE À JOUR ICI
                if (killer_moves[depth][0] != idx) {
                    killer_moves[depth][1] = killer_moves[depth][0];
                    killer_moves[depth][0] = idx;
                }
                history_heuristic[idx] += (depth * depth);
                debug_cutoff_count++;
                break; 
            }
        }
    }

    // 4. TT SAVE
    if (depth > 1) {
        int flag;
        if (best_val <= alpha_orig) flag = TT_UPPERBOUND; 
        else if (best_val >= beta) flag = TT_LOWERBOUND;
        else flag = TT_EXACT;
        
        // Maintenant best_move_this_node est défini !
        tt_save(g->current_hash, depth, best_val, flag, best_move_this_node);
    }

    return best_val;
}

// --- MODULE VCF SÉCURISÉ ---

/* 
   Recherche récursive de victoire forcée.
   MAINTENANT AVEC TIMEOUT CHECK !
*/
int vcf_search(game *g, int depth, int player, int ia_player, clock_t start_time) {
    // SÉCURITÉ : Si on dépasse le temps pendant le VCF, on abandonne immédiatement.
    // On retourne 0 (pas de victoire trouvée) pour laisser le Minimax finir proprement.
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) return 0;

    // 1. Vérification Victoire Immédiate
    int current_eval = evaluate_board(g, ia_player);
    if (abs(current_eval) > WIN_SCORE / 2) return 1; 
    
    if (depth == 0) return 0; 

    int opponent = (player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, player, depth, -1);
    
    // 3. Filtrage VCF
    int vcf_moves_count = 0;
    MoveCandidate vcf_moves[MAX_BOARD];

    for (int i = 0; i < count; i++) {
        int idx = moves[i].index;
        int x = GET_X(idx);
        int y = GET_Y(idx);

        if (player == ia_player) {
            // --- TOUR DE L'ATTAQUANT (IA) ---
            // On ne garde QUE les coups qui créent une menace mortelle (4 ou 5)
            // Seuil 5500 : Couvre Broken 4 (6000) et Open 4.
            
            g->board[idx] = player;
            int attack_score = get_point_score(g, x, y, player);
            g->board[idx] = EMPTY;

            if (attack_score >= 5500) {
                vcf_moves[vcf_moves_count++] = moves[i];
            }
        } 
        else {
            // --- TOUR DU DÉFENSEUR (HUMAIN) ---
            // Il DOIT bloquer. On ne garde que les coups qui ont un score défensif énorme.
            // (C'est-à-dire : si je ne joue pas là, l'IA gagne au prochain tour).
            
            g->board[idx] = ia_player; // On simule que l'IA joue là
            int threat_score = get_point_score(g, x, y, ia_player);
            g->board[idx] = EMPTY;

            // Si l'IA marque > 5500 ici, c'est que c'est un point critique à défendre
            if (threat_score >= 5500) {
                vcf_moves[vcf_moves_count++] = moves[i];
            }
        }
    }

    // Si l'attaquant n'a pas de coup forcing -> La branche meurt (Pas de VCF ici)
    if (player == ia_player && vcf_moves_count == 0) return 0;

    // Si le défenseur n'a pas de coup de défense -> Echec et Mat (Victoire IA)
    if (player != ia_player && vcf_moves_count == 0) return 1;

    // 4. Exploration Récursive
    for (int i = 0; i < vcf_moves_count; i++) {
        int idx = vcf_moves[i].index;
        MoveUndo undo;
        
        apply_move(g, idx, player, &undo);
        
        // CORRECTION : On propage start_time dans la récursion
        int result = vcf_search(g, depth - 1, opponent, ia_player, start_time);
        
        undo_move(g, player, &undo);

        if (player == ia_player) {
            if (result == 1) return 1; 
        } else {
            if (result == 0) return 0;
        }
    }
    
    return (player != ia_player); 
}

/* Fonction wrapper pour lancer le VCF et récupérer le coup */
int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;
    MoveCandidate moves[MAX_BOARD];
    
    // On génère tous les coups possibles pour l'IA
    int count_ia = generate_moves(g, moves, ia_player, 0, -1);

    // --- ÉTAPE 1 : VICTOIRE IMMÉDIATE (MAT EN 1) ---
    // "XXX_X" ou "XXXX_" -> On gagne tout de suite.
    for (int i = 0; i < count_ia; i++) {
        int idx = moves[i].index;
        g->board[idx] = ia_player;
        int score = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
        g->board[idx] = EMPTY;

        if (score >= WIN_SCORE) {
            #ifdef DEBUG
            printf("[INSTANT WIN] Victoire immédiate trouvée en (%d, %d)\n", GET_X(idx), GET_Y(idx));
            #endif
            return idx;
        }
    }

    // --- ÉTAPE 2 : DÉFAITE IMMÉDIATE (MAT EN 1 ADVERSE) ---
    // Si l'adversaire peut gagner tout de suite, on DOIT bloquer.
    // On utilise les mêmes coups (cases vides), mais on évalue pour l'adversaire.
    for (int i = 0; i < count_ia; i++) {
        int idx = moves[i].index;
        g->board[idx] = opponent;
        int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
        g->board[idx] = EMPTY;

        if (score >= WIN_SCORE) {
            #ifdef DEBUG
            printf("[INSTANT BLOCK] Blocage de victoire immédiate en (%d, %d)\n", GET_X(idx), GET_Y(idx));
            #endif
            return idx;
        }
    }

    // --- ÉTAPE 3 : VCF OFFENSIF (Victoire Forcée en X coups) ---
    // On cherche si on peut gagner en enchaînant les menaces.
    for (int max_depth = 3; max_depth <= 17; max_depth += 2) {
        // Check Time : On garde du temps pour le Minimax
        if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > (TIME_LIMIT_MS / 2)) break; 

        for (int i = 0; i < count_ia; i++) {
            int idx = moves[i].index;
            
            // Pré-filtre : On ne teste que les coups d'attaque (Open 3, Broken 3, Open 4...)
            g->board[idx] = ia_player;
            int score = get_point_score(g, GET_X(idx), GET_Y(idx), ia_player);
            g->board[idx] = EMPTY;

            if (score < 5500) continue; 

            MoveUndo undo;
            apply_move(g, idx, ia_player, &undo);
            int win = vcf_search(g, max_depth, opponent, ia_player, start_time);
            undo_move(g, ia_player, &undo);

            if (win) {
                #ifdef DEBUG
                // AFFICHAGE DE LA PROFONDEUR VCF
                printf(">>> VCF OFFENSIF TROUVÉ ! Depth: %d | Coup: (%d, %d)\n", max_depth, GET_X(idx), GET_Y(idx));
                #endif
                return idx;
            }
        }
    }

    // --- ÉTAPE 4 : VCF DÉFENSIF (L'adversaire a-t-il une victoire forcée ?) ---
    // Si je ne peux pas gagner, je vérifie si je vais perdre forcément.
    
    // On génère les coups du point de vue de l'adversaire pour trouver ses menaces
    MoveCandidate moves_opp[MAX_BOARD];
    int count_opp = generate_moves(g, moves_opp, opponent, 0, -1);

    for (int i = 0; i < count_opp; i++) {
        int idx = moves_opp[i].index;
        
        g->board[idx] = opponent;
        int score = get_point_score(g, GET_X(idx), GET_Y(idx), opponent);
        g->board[idx] = EMPTY;

        // Si l'adversaire a un coup menaçant (Open 3 ou mieux)
        if (score >= 5500) {
            
            // On vérifie si ce coup déclenche une victoire forcée pour lui
            MoveUndo undo;
            apply_move(g, idx, opponent, &undo);
            
            // On cherche une victoire pour LUI (opponent)
            // Profondeur limitée à 9 pour la rapidité défensive
            int opponent_wins = vcf_search(g, 9, ia_player, opponent, start_time);
            
            undo_move(g, opponent, &undo);

            if (opponent_wins) {
                #ifdef DEBUG
                printf(">>> VCF DÉFENSIF : L'adversaire gagne en Depth 9 via (%d, %d). BLOCAGE.\n", GET_X(idx), GET_Y(idx));
                #endif
                return idx; // On joue sur sa case clé
            }
        }
    }

    return -1; 
}

/* Fonction principale appelée par le main */
void makeIaMove(game *gameData, int ia_player) {
    clock_t start = clock();
    
    // 1. VCF / VICTOIRE IMMÉDIATE
    int vcf_move = solve_vcf(gameData, ia_player, start);
    if (vcf_move != -1) {
        gameData->board[vcf_move] = ia_player;
        gameData->last_move_idx = vcf_move;
        
        // On loggue clairement que c'était un coup spécial
        printf(">>> IA joue le coup VCF/WIN en (%d, %d) [Temps: %.3fs]\n", 
               GET_X(vcf_move), GET_Y(vcf_move), 
               (double)(clock() - start) / CLOCKS_PER_SEC);
        return; // ON SORT ICI, on n'affiche pas les stats Minimax
    }

    // 2. MINIMAX (Si pas de victoire immédiate)
    // --- ITERATIVE DEEPENING AVEC ASPIRATION WINDOWS ---
    // On stocke le score de la profondeur précédente pour prédire le futur
    int prev_score = 0; 

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {
        
        // Préparation de la fenêtre d'aspiration
        int alpha_start = INT_MIN;
        int beta_start = INT_MAX;
        int window = 500; // Largeur de la fenêtre (à ajuster, 500 est standard pour Gomoku)

        // À partir de profondeur 4, on tente de deviner le score
        if (depth > 2) {
            alpha_start = prev_score - window;
            beta_start = prev_score + window;
        }

        // Boucle de "Retry" : Si la fenêtre échoue, on recommence avec l'infini
        // false au départ, true tant qu'on doit chercher
        bool need_research = true; 
        
        // Sauvegarde des coups pour ne pas regénérer en cas de retry
        MoveCandidate moves[MAX_BOARD];
        int previous_best_move = best_move_idx;
        int count = generate_moves(gameData, moves, ia_player, depth, previous_best_move);

        // Fallback premier coup
        if (best_move_idx == -1 && count > 0) best_move_idx = moves[0].index;
        if (count == 0) return;

        while (need_research) {
            // Par défaut, on suppose qu'on réussira du premier coup, on ne recommencera pas
            need_research = false; 

            int alpha = alpha_start;
            int beta = beta_start;
            int current_best_idx = -1;
            int current_best_score = INT_MIN;
            
            bool time_out = false;
            game working_game = *gameData; 

            for (int i = 0; i < count; i++) {
                int idx = moves[i].index;
                MoveUndo undo;

                apply_move(&working_game, idx, ia_player, &undo);
                
                // Appel PVS (qui utilise alpha/beta)
                int val = minimax(&working_game, depth - 1, alpha, beta, false, ia_player, start);
                
                undo_move(&working_game, ia_player, &undo);

                if (val == -2) { 
                    time_out = true;
                    break; 
                }

                if (val > current_best_score) {
                    current_best_score = val;
                    current_best_idx = idx;
                }
                
                // Mise à jour dynamique d'alpha (classique)
                if (val > alpha) alpha = val;
            }

            // --- GESTION DU TIMEOUT ---
            if (time_out) {
                #ifdef DEBUG
                    printf("Timeout at depth %d. Keeping best move from depth %d.\n", depth, depth-2);
                #endif
                goto end_search; // On sort de tout
            }

            // --- GESTION ASPIRATION FAILURE (Le cœur du système) ---
            // Si le score est hors de la fenêtre, notre prédiction était mauvaise.
            if (depth > 2 && (current_best_score <= alpha_start || current_best_score >= beta_start)) {
                #ifdef DEBUG
                    printf("Aspiration Fail at depth %d (Score %d outside [%d, %d]). Re-searching full window.\n", 
                           depth, current_best_score, alpha_start, beta_start);
                #endif
                if (alpha_start == INT_MIN && beta_start == INT_MAX) {
                    need_research = false; 
                } 
                else {
                    // On élargit la fenêtre pour la prochaine tentative
                    alpha_start = INT_MIN;
                    beta_start = INT_MAX;
                    need_research = true; 
                    continue; 
                }
            }

            // --- MODIFICATION 1 : EARLY EXIT (Arrêt sur Victoire) ---
            // Si on a trouvé une victoire forcée, on arrête TOUT DE SUITE.
            // Pas besoin d'aller à Depth 10 si on gagne à Depth 4.
            if (current_best_score > WIN_SCORE - 5000) {
                #ifdef DEBUG
                printf("Winning move found at depth %d. Stopping search.\n", depth);
                #endif
                best_move_idx = current_best_idx;
                goto play_move; // On saute directement au jeu
            }
            
            // Si on arrive ici, le score est valide ou on a fini le re-search
            best_move_idx = current_best_idx;
            prev_score = current_best_score; // On mémorise pour la prochaine profondeur

            #ifdef DEBUG
                printf("Depth %d complete. Best: %d. Nodes: %lld, Cutoffs: %lld.\n", 
                    depth, current_best_score, debug_node_count, debug_cutoff_count);
            #endif
            
            if (current_best_score > WIN_SCORE / 2) goto end_search;
        } // Fin while(retry)

        // Check Time
        if ((clock() - start) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) break;
    }

    end_search:;

play_move:
    if (best_move_idx != -1) {
        int x = GET_X(best_move_idx);
        int y = GET_Y(best_move_idx);
        
        MoveUndo final_undo;
        apply_move(gameData, best_move_idx, ia_player, &final_undo);

        drawSquare(windows, x, y, ia_player);
        
        if (final_undo.captured_count > 0) {
            for (int k = 0; k < final_undo.captured_count; k++) {
                int cap_idx = final_undo.captured_indices[k];
                drawSquare(windows, GET_X(cap_idx), GET_Y(cap_idx), EMPTY);
            }
        }

        windows->changed = true; 
        
        #ifdef DEBUG
            printf("IA plays at (%d, %d)\n", x, y);
        #endif
    } else {
        #ifdef DEBUG
            printf("IA cannot move.\n");
        #endif
    }

    clock_t end = clock();
    gameData->ia_timer.elapsed = (double)(end - start) / CLOCKS_PER_SEC;
}
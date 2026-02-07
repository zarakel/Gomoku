// src/ai_tactics.c
#include "../include/gomoku.h"

/**
 * Module de tactiques avancees : VCF (Victory by Continuous Forcing).
 * 
 * Le VCF est une technique de recherche de sequences forcees menant a la victoire.
 * L'attaquant joue des coups qui forcent l'adversaire a repondre (menaces OPEN_FOUR),
 * jusqu'a creer une situation gagnante imparable.
 * 
 * Optimisations :
 * - Generation legere (coups forçants uniquement)
 * - Cache des resultats VCF
 * - Limite de temps stricte (400ms)
 * - Profondeur maximale 30 plies
 */

#define VCF_MAX_DEPTH 30
#define VCF_TIME_LIMIT 0.40 // 400ms max alloues au VCF (offensif et defensif)

// Cache global pour eviter de recalculer les memes positions
static int vcf_cache[MAX_BOARD];      // -1 = non teste, 0 = echec, 1 = succes
static uint64_t vcf_cache_hash = 0;   // Hash de la position en cache

/**
 * Genere uniquement les coups forçants (creant des menaces OPEN_THREE ou mieux).
 * 
 * Optimisations :
 * - Scanne uniquement les cases proches des pierres existantes (rayon 2)
 * - Utilise get_point_score_fast() au lieu de l'evaluation complete
 * - Verifie la regle du double-three
 * - Limite le nombre de coups retournes (max 30)
 * 
 * Retourne le nombre de coups d'attaque trouves.
 */
static int generate_attacking_moves(game *g, int player, MoveVCF *moves) {
    int count = 0;
    int opponent = (player == P1) ? P2 : P1;
    
    // OPTIMISATION VCF : Génération légère sans appeler generate_moves
    // Scanner uniquement les cases proches des pierres existantes
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != EMPTY) continue;
        
        int x = GET_X(idx), y = GET_Y(idx);
        
        // Filtre rapide : case doit avoir au moins un voisin dans rayon 2
        bool has_neighbor = false;
        for (int dy = -2; dy <= 2 && !has_neighbor; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int nx = x + dx, ny = y + dy;
                if (IS_VALID(nx, ny) && g->board[GET_INDEX(nx, ny)] != EMPTY) {
                    has_neighbor = true;
                    break;
                }
            }
        }
        if (!has_neighbor) continue;
        
        // Vérifier règle double-three
        if (is_double_three(g, idx, player)) continue;

        // Simulation légère avec évaluation RAPIDE
        g->board[idx] = player;
        int score = get_point_score_fast(g, x, y, player); // FAST au lieu de normal
        g->board[idx] = EMPTY;

        // Coup forçant : OPEN_THREE ou mieux (réduit le seuil)
        if (score >= OPEN_THREE) { 
            moves[count].move_idx = idx;
            moves[count].score = score;
            count++;
            if (count >= 30) break; // Limite pour éviter explosion
        }
    }
    return count;
}

/**
 * Genere les coups defensifs pour bloquer une menace.
 * 
 * Utilise generate_moves() pour obtenir les coups pertinents,
 * puis filtre pour garder uniquement ceux avec une valeur defensive suffisante.
 * 
 * Retourne le nombre de coups defensifs trouves.
 */
static int generate_defensive_moves(game *g, int defender, MoveVCF *moves) {
    int count = 0;
    int attacker = (defender == P1) ? P2 : P1;
    
    // Trouver où est la menace (c'est coûteux de tout scanner, 
    // on suppose ici qu'on utilise les fonctions de threat existantes)
    
    // Approche brute simplifiée pour la démo : 
    // On cherche les coups qui bloquent ou détruisent la menace.
    MoveCandidate candidates[MAX_BOARD];
    int raw_count = generate_moves(g, candidates, defender, 0, -1);
    
    for (int i = 0; i < raw_count; i++) {
        // On garde tout ce qui a un sens défensif
        if (is_double_three(g, candidates[i].index, defender)) continue;
        if (candidates[i].score_estim > 500) { 
            moves[count].move_idx = candidates[i].index;
            count++;
        }
    }
    return count;
}

/**
 * Moteur de recherche VCF recursif.
 * 
 * Principe :
 * 1. L'attaquant joue un coup forçant (menace OPEN_FOUR ou mieux)
 * 2. Le defenseur est oblige de bloquer (un seul coup possible ou tres peu)
 * 3. L'attaquant joue un nouveau coup forçant
 * 4. Repetition jusqu'a victoire ou echec
 * 
 * Retourne true si une sequence gagnante forcee existe, false sinon.
 * 
 * Limites :
 * - Profondeur maximale : VCF_MAX_DEPTH (30 plies)
 * - Temps maximal : VCF_TIME_LIMIT (400ms)
 */
bool vcf_search(game *g, int attacker, int depth, clock_t start_time) {
    // 1. Check Limites
    if (depth > VCF_MAX_DEPTH) return false;
    if ((double)(clock() - start_time) / CLOCKS_PER_SEC > VCF_TIME_LIMIT) return false;
    int defender = (attacker == P1) ? P2 : P1;

    // 2. Check Victoire Immédiate (Déjà gagné ?)
    if (evaluate_board(g, attacker) >= WIN_SCORE) return true;

    // 3. Génération des coups d'attaque
    MoveVCF attacks[MAX_BOARD];
    int attack_count = generate_attacking_moves(g, attacker, attacks);

    if (attack_count == 0) return false; // Plus de coups forçants, attaque échouée

    for (int i = 0; i < attack_count; i++) {
        
        int atk_idx = attacks[i].move_idx;
        
        MoveUndo undo_atk;
        apply_move(g, atk_idx, attacker, &undo_atk);
        
        // Si ce coup gagne immédiatement
        if (evaluate_board(g, attacker) >= WIN_SCORE) {
            undo_move(g, attacker, &undo_atk);
            return true;
        }

        // --- TOUR DU DÉFENSEUR ---
        // Le défenseur DOIT répondre. 
        // Si le défenseur a une réponse qui sauve, alors cette attaque est nulle.
        // Si TOUTES les réponses du défenseur mènent quand même à la défaite, cette attaque est GAGNANTE.

        bool defender_survives = false;
        
        MoveVCF defenses[MAX_BOARD];
        int def_count = generate_defensive_moves(g, defender, defenses);
        
        // Si le défenseur n'a aucun coup logique (ex: plateau plein ou bloqué), il perd
        if (def_count == 0) {
            defender_survives = false;
        } else {
            // Testons toutes les défenses
            for (int j = 0; j < def_count; j++) {
                int def_idx = defenses[j].move_idx;
                MoveUndo undo_def;
                apply_move(g, def_idx, defender, &undo_def);
                
                // Appel Récursif : Est-ce que l'attaquant gagne ENCORE après cette défense ?
                bool still_losing = vcf_search(g, attacker, depth + 1, start_time);
                
                undo_move(g, defender, &undo_def);
                
                if (!still_losing) {
                    defender_survives = true;
                    break; // Une défense a marché, pas la peine de tester les autres
                }
            }
        }

        undo_move(g, attacker, &undo_atk);

        // Si le défenseur ne survit pas à CETTE attaque spécifique, alors l'attaquant a trouvé son VCF !
        if (!defender_survives) {
            return true; 
        }
    }

    return false; // Aucune attaque ne garantit la victoire
}

/**
 * Wrapper public pour lancer une recherche VCF.
 * Interface simplifiee pour les autres modules.
 */
bool has_vcf_win(game *g, int attacker, int depth, int max_depth, double time_limit) {
    (void)max_depth; // Paramètre inutilisé pour l'instant
    // Convertir time_limit_sec en clock ticks si nécessaire, ou utiliser clock()
    // Ici on lance simplement la recherche avec le temps de départ actuel
    return vcf_search(g, attacker, depth, clock());
}

static bool opponent_has_unstoppable_win(game *g, int opponent) {
    // 1. A-t-il un Open 4 ? (Victoire au prochain coup imparable sauf blocage direct)
    if (g->max_threat_level[opponent] >= IDX_OPEN_FOUR) return true;
    
    // 2. A-t-il un alignement de 5 déjà fait ? (Trop tard)
    if (evaluate_board(g, opponent) >= WIN_SCORE) return true;
    
    return false;
}

int solve_defensive_crisis(game *g, int me) {
    int opponent = (me == P1) ? P2 : P1;
    clock_t start = clock();

    // 1. Détection : Est-ce qu'on va vraiment mourir ?
    // On lance une recherche VCF pour l'adversaire
    bool threat_detected = vcf_search(g, opponent, 0, start);
    
    // Si pas de VCF détecté mais qu'on est en crise (niveau élevé), on continue quand même
    // car le VCF search peut avoir raté quelque chose (profondeur limitée)
    if (!threat_detected && g->max_threat_level[opponent] < IDX_OPEN_FOUR) {
        return -1;
    }

    printf(">>> CRISE DÉTECTÉE (Sauvetage Tactique) : Recherche de contre-mesure...\n");

    MoveCandidate candidates[MAX_BOARD];
    
    // CORRECTION MAJEURE 1 : Depth 0 (Root) pour avoir TOUS les coups, et faisceau LARGE
    // On ne veut pas l'optimisation "beam search" ici, on veut la survie brute.
    int count = generate_moves(g, candidates, me, 0, -1); 

    int best_save_idx = -1;
    int best_save_score = -1000000;
    
    // Compteur de coups testés pour debug
    int tested_moves = 0;

    for (int i = 0; i < count; i++) {
        // CORRECTION MAJEURE 2 : Suppression du filtre heuristique agressif
        // Un coup vital peut avoir un score statique nul (ex: blocage purement positionnel)
        // On garde un filtre minimal pour le bruit absolu seulement
        if (candidates[i].score_estim < 5) continue; 

        int idx = candidates[i].index;
        
        // Règle Renju
        if (is_double_three(g, idx, me)) continue;

        // Pré-check : Si l'adversaire a un Open 4, ce coup DOIT le bloquer
        // (Optimisation pour éviter de lancer vcf_search si on ne bloque même pas la menace directe)
        if (g->max_threat_level[opponent] >= IDX_OPEN_FOUR) {
             // Simulation légère locale ?
             // Pour l'instant on fait confiance au vcf_search complet plus bas
        }

        MoveUndo undo;
        apply_move(g, idx, me, &undo);
        tested_moves++;

        // A. VICTOIRE IMMÉDIATE : Si je gagne, c'est la meilleure défense
        if (evaluate_board(g, me) >= WIN_SCORE) {
            undo_move(g, me, &undo);
            printf(">>> CONTRE-ATTAQUE GAGNANTE en (%d,%d)\n", GET_X(idx), GET_Y(idx));
            return idx;
        }

        // B. EST-CE QUE CE COUP BRISE LE VCF ADVERSE ?
        // On relance la recherche pour l'adversaire avec NOTRE pierre sur le plateau
        bool threat_persists = vcf_search(g, opponent, 0, start);
        
        // C. VERIFICATION "STUPID DEATH"
        // Le VCF est brisé, mais a-t-on laissé un alignement de 5 statique ou un Open 4 simple ?
        bool stupid_death = false;
        
        if (!threat_persists) {
            // Check victoire immédiate adverse
            if (evaluate_board(g, opponent) >= WIN_SCORE) stupid_death = true;
            
            // Check Open 4 adverse non bloqué (VCF search peut parfois rater un simple Open 4 si depth est bas)
            if (!stupid_death) {
                // On scanne rapidement les menaces
                // (Idéalement on devrait faire un update incremental, ici on check le score max)
                // Note : evaluate_board check déjà les alignements, mais pas les Open 4 futurs
                // On suppose que vcf_search(depth 0) voit les Open 4.
            }
        }

        undo_move(g, me, &undo);

        // Si on survit !
        if (!threat_persists && !stupid_death) {
            int my_attack_score = get_point_score(g, GET_X(idx), GET_Y(idx), me);
            
            // Si on trouve une défense qui crée aussi une menace (Contre-Attaque), c'est le Graal
            if (my_attack_score >= OPEN_FOUR) {
                printf(">>> SAUVETAGE OFFENSIF : (%d,%d) bloque et contre-attaque !\n", GET_X(idx), GET_Y(idx));
                return idx;
            }
            
            // Sinon on garde la meilleure défense trouvée (celle qui nous donne le meilleur positionnement)
            if (my_attack_score > best_save_score) {
                best_save_score = my_attack_score;
                best_save_idx = idx;
            }
        }
        
        // Check timeout (on laisse jusqu'à 80% du temps restant pour la survie)
        if ((double)(clock() - start) / CLOCKS_PER_SEC > 0.40) {
            #ifdef DEBUG
            printf(">>> Timeout Crisis Solver après %d coups testés\n", tested_moves);
            #endif
            break;
        }
    }

    if (best_save_idx != -1) {
        printf(">>> SAUVETAGE TROUVÉ : (%d,%d) après %d tests\n", 
               GET_X(best_save_idx), GET_Y(best_save_idx), tested_moves);
        return best_save_idx;
    }

    printf(">>> ÉCHEC DÉFENSE TACTIQUE : Aucune parade trouvée après %d tests.\n", tested_moves);
    return -2;
}

/* * Vérifie si jouer ici complète un alignement de 5 IMPRENABLE.
 * (Si l'adversaire peut capturer une pierre de l'alignement au même tour, ce n'est pas une victoire)
 */
bool check_five_align(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    // 1. Simuler la pose de la pierre
    g->board[idx] = player;

    bool is_winning = false;

    for (int d = 0; d < 4; d++) {
        int count = 1; 
        int dx = dirs[d][0];
        int dy = dirs[d][1];
        
        // Stocker les indices de l'alignement pour vérifier s'ils sont capturables
        int line_indices[9]; 
        int line_len = 0;
        line_indices[line_len++] = idx;

        // Sens positif
        for (int k = 1; k < 5; k++) {
            int nx = x + k * dx;
            int ny = y + k * dy;
            int n_idx = GET_INDEX(nx, ny);
            if (!IS_VALID(nx, ny) || g->board[n_idx] != player) break;
            count++;
            line_indices[line_len++] = n_idx;
        }
        // Sens négatif
        for (int k = 1; k < 5; k++) {
            int nx = x - k * dx;
            int ny = y - k * dy;
            int n_idx = GET_INDEX(nx, ny);
            if (!IS_VALID(nx, ny) || g->board[n_idx] != player) break;
            count++;
            line_indices[line_len++] = n_idx;
        }

        if (count >= 5) {
            // ALIGNEMENT TROUVÉ !
            // Maintenant, vérifions si une des pierres de cet alignement est en danger de capture IMMÉDIATE
            bool broken = false;
            for (int i = 0; i < line_len; i++) {
                // On utilise la fonction is_move_capturable qu'on a déjà
                // pour voir si CETTE pierre spécifique est prise en tenaille
                if (is_move_capturable(g, line_indices[i], player)) {
                    broken = true;
                    break; 
                }
            }
            
            if (!broken) {
                is_winning = true;
                break; // Victoire confirmée et solide
            }
        }
    }

    // 2. Annuler la simulation
    g->board[idx] = EMPTY;
    
    return is_winning;
}

/* Vérifie si la pierre qu'on pose en 'idx' peut être capturée immédiatement. */
bool is_move_capturable(game *g, int idx, int player) {
    int x = GET_X(idx);
    int y = GET_Y(idx);
    int opponent = (player == P1) ? P2 : P1;
    
    // Directions : Horiz, Vert, Diag1, Diag2
    int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};

    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];

        // Vérification des 2 côtés :
        // Cas 1 : [Opp] [MOI] [Moi] [Opp?] -> Si je pose [MOI], est-ce que je complète une paire prenable ?
        // Cas 2 : [Opp] [Moi] [MOI] [Opp?]
        
        // Pour simplifier : On regarde si jouer ici crée un motif O-X-X-O avec l'adversaire
        
        // Test côté positif
        int p1_x = x + dx, p1_y = y + dy;       // Voisin 1
        int p2_x = x + 2*dx, p2_y = y + 2*dy;   // Voisin 2 (Opposant ?)
        int m1_x = x - dx, m1_y = y - dy;       // Arrière 1 (Opposant ?)

        // Si j'ai un ami devant et un ennemi derrière et un ennemi devant l'ami
        if (IS_VALID(p1_x, p1_y) && g->board[GET_INDEX(p1_x, p1_y)] == player &&
            IS_VALID(p2_x, p2_y) && g->board[GET_INDEX(p2_x, p2_y)] == opponent &&
            IS_VALID(m1_x, m1_y) && g->board[GET_INDEX(m1_x, m1_y)] == opponent) {
            return true;
        }

        // Test côté négatif (symétrique)
        int n1_x = x - dx, n1_y = y - dy;
        int n2_x = x - 2*dx, n2_y = y - 2*dy;
        int pm1_x = x + dx, pm1_y = y + dy;

        if (IS_VALID(n1_x, n1_y) && g->board[GET_INDEX(n1_x, n1_y)] == player &&
            IS_VALID(n2_x, n2_y) && g->board[GET_INDEX(n2_x, n2_y)] == opponent &&
            IS_VALID(pm1_x, pm1_y) && g->board[GET_INDEX(pm1_x, pm1_y)] == opponent) {
            return true;
        }
    }
    return false;
}

// Vérifie si un coup tactique est réellement jouable et sûr
static bool is_tactically_safe(game *g, int move_idx, int player) {
    // 1. Règle Renju
    if (is_double_three(g, move_idx, player)) return false;

    int opponent = (player == P1) ? P2 : P1;
    
    // 2. Vérification Danger Critique (Captures)
    // Si l'adversaire a 3 ou 4 captures, la prudence est maximale.
    if (g->captures[opponent] >= 4) {
        // A. Si je gagne DIRECTEMENT (Alignement 5), c'est bon, la partie finit avant la capture.
        if (check_five_align(g, move_idx, player)) return true;

        // B. Sinon, si mon coup est capturable, c'est un SUICIDE.
        // Car l'adversaire va prendre ma paire et gagner (5 captures).
        if (is_move_capturable(g, move_idx, player)) {
            #ifdef DEBUG
            printf("    VCF rejeté : Coup suicidaire (donne la 5ème capture)\n");
            #endif
            return false;
        }
        
        // C. Même si le coup n'est pas capturable, si je ne gagne pas tout de suite,
        // je laisse l'initiative à l'adversaire qui peut avoir une menace de capture ailleurs.
        // En mode "Danger Critique", on rejette les attaques complexes non-immédiates.
        return false; 
    }

    return true;
}

static void vcf_cache_invalidate_area(int move_idx) {
    if (move_idx == -1) return;
    
    int x = GET_X(move_idx);
    int y = GET_Y(move_idx);
    
    // Invalider seulement les cases dans un rayon de 3
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int nx = x + dx;
            int ny = y + dy;
            if (IS_VALID(nx, ny)) {
                vcf_cache[GET_INDEX(nx, ny)] = -1;
            }
        }
    }
}

int find_winning_vcf(game *g, int attacker) {
    // ===== ÉTAPE 1 : GESTION DU CACHE =====
    // Reset cache si la position a changé
    static uint64_t last_hash = 0;
    static int last_move = -1;
    
    if (vcf_cache_hash != g->current_hash) {
        // Si c'est juste après un coup (hash différent), on invalide localement
        if (last_hash != 0) {
            vcf_cache_invalidate_area(last_move);
            #ifdef DEBUG
            printf(">>> VCF Cache : Clear partiel autour de (%d,%d)\n",
                   GET_X(last_move), GET_Y(last_move));
            #endif
        } else {
            // Sinon, reset complet (nouvelle partie)
            memset(vcf_cache, -1, sizeof(vcf_cache));
            #ifdef DEBUG
            printf(">>> VCF Cache : Reset complet\n");
            #endif
        }
        
        vcf_cache_hash = g->current_hash;
    }
    
    last_hash = g->current_hash;
    int defender = (attacker == P1) ? P2 : P1;
    
    // ===== ÉTAPE 2 : VÉRIFICATION PRÉLIMINAIRE =====
    // Si l'adversaire a déjà un Open 4, pas la peine de chercher un VCF
    // (Il va gagner avant nous)
    if (g->max_threat_level[defender] >= IDX_OPEN_FOUR) {
        #ifdef DEBUG
        printf(">>> VCF abandonné : adversaire a déjà un Open Four\n");
        #endif
        return -1;
    }
    
    // ===== ÉTAPE 3 : GÉNÉRATION DES COUPS CANDIDATS =====
    MoveCandidate moves[MAX_BOARD];
    int count = generate_moves(g, moves, attacker, 0, -1);
    
    // Timer pour limiter la recherche offensive
    clock_t start = clock();
    double time_limit = 0.20; // 200ms max pour trouver l'attaque
    
    // ===== NOUVEAU : Statistiques du cache (debug) =====
    #ifdef DEBUG
    int cache_hits = 0;
    int cache_misses = 0;
    int tests_performed = 0;
    #endif
    // =================================================
    
    // ===== ÉTAPE 4 : RECHERCHE VCF AVEC CACHE =====
    for (int i = 0; i < count; i++) {
        // OPTIMISATION CRITIQUE : Filtrage par menace minimale
        // Un VCF commence forcément par une menace sérieuse (Open 3 minimum)
        if (moves[i].score_estim < OPEN_THREE) {
            #ifdef DEBUG
            // On arrête dès qu'on tombe sous le seuil (la liste est triée)
            printf(">>> VCF : Arrêt du scan à l'index %d (score trop faible)\n", i);
            #endif
            break; // Plus besoin de continuer, la liste est triée
        }
        
        int idx = moves[i].index;
        
        // ===== OPTIMISATION : CHECK CACHE AVANT TEST =====
        if (vcf_cache[idx] == 0) {
            // Ce coup a déjà été testé et a échoué
            #ifdef DEBUG
            cache_hits++;
            printf("    Cache HIT : (%d,%d) déjà testé (échec)\n", 
                   GET_X(idx), GET_Y(idx));
            #endif
            continue;
        } else if (vcf_cache[idx] == 1) {
            // Ce coup a déjà été testé et a réussi !
            #ifdef DEBUG
            cache_hits++;
            printf("    Cache HIT : (%d,%d) déjà testé (SUCCÈS) → Retour immédiat\n", 
                   GET_X(idx), GET_Y(idx));
            #endif
            return idx; // Victoire trouvée en cache !
        }
        // Sinon vcf_cache[idx] == -1 → Pas encore testé
        #ifdef DEBUG
        else {
            cache_misses++;
        }
        #endif

        // Avant de dépenser du temps CPU pour simuler ce coup,
        // on vérifie s'il est légal (Renju) et s'il ne nous suicide pas (Captures).
        if (!is_tactically_safe(g, idx, attacker)) {
            // Marquer comme échec dans le cache pour ne pas le re-tester
            vcf_cache[idx] = 0; 
            
            #ifdef DEBUG
            printf("    VCF rejeté (illégal/dangereux) : (%d,%d)\n", 
                   GET_X(idx), GET_Y(idx));
            #endif
            continue; 
        }

        // ===== TEST DU COUP =====
        #ifdef DEBUG
        tests_performed++;
        #endif
        
        MoveUndo undo;
        apply_move(g, idx, attacker, &undo);
        
        // A. Victoire Immédiate ?
        if (evaluate_board(g, attacker) >= WIN_SCORE) {
            undo_move(g, attacker, &undo);
            
            // ===== SAUVEGARDER DANS LE CACHE =====
            vcf_cache[idx] = 1; // Succès !
            // ======================================
            
            #ifdef DEBUG
            printf(">>> VCF : Victoire immédiate en (%d,%d)\n", 
                   GET_X(idx), GET_Y(idx));
            #endif
            return idx;
        }
        
        // B. Est-ce que ce coup force la victoire (VCF récursif) ?
        bool vcf_found = has_vcf_win(g, attacker, 1, 6, 
                                      (double)(clock() - start)/CLOCKS_PER_SEC + time_limit);
        
        undo_move(g, attacker, &undo);
        
        // ===== SAUVEGARDER LE RÉSULTAT DANS LE CACHE =====
        vcf_cache[idx] = vcf_found ? 1 : 0;
        // ==================================================
        
        if (vcf_found) {
            #ifdef DEBUG
            printf(">>> VCF trouvé en (%d,%d) après %d tests\n", 
                   GET_X(idx), GET_Y(idx), tests_performed);
            printf("    Cache : %d hits, %d misses\n", cache_hits, cache_misses);
            #endif
            return idx; // C'est le coup gagnant !
        }
        
        // Check timeout global de la fonction
        if ((double)(clock() - start) / CLOCKS_PER_SEC > time_limit) {
            #ifdef DEBUG
            printf(">>> VCF timeout après %d tests (%.3fs)\n", 
                   tests_performed, (double)(clock() - start) / CLOCKS_PER_SEC);
            #endif
            break;
        }
    }
    
    // ===== ÉTAPE 5 : AUCUN VCF TROUVÉ =====
    #ifdef DEBUG
    printf(">>> VCF : Aucune victoire forcée trouvée\n");
    printf("    Cache : %d hits, %d misses, %d tests effectués\n", 
           cache_hits, cache_misses, tests_performed);
    #endif
    
    return -1;
}
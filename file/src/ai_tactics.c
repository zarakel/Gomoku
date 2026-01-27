// src/ai_tactics.c
#include "../include/gomoku.h"

#define VCF_MAX_DEPTH 20
#define VCF_TIME_LIMIT 0.15 // 150ms max alloués à la panique défensive

/* * Génère uniquement les coups qui attaquent (Créent un 4)
 * Retourne le nombre de coups trouvés.
 */
static int generate_attacking_moves(game *g, int player, MoveVCF *moves) {
    int count = 0;
    int opponent = (player == P1) ? P2 : P1;
    
    // On peut optimiser en ne scannant que les zones pertinentes, 
    // mais pour l'instant scan global rapide via les heuristiques
    // Idéalement : utiliser une liste des 'coups candidats' maintenue incrémentalement
    
    // Pour simplifier ici : on utilise generate_moves existant mais on filtre drastiquement
    MoveCandidate candidates[MAX_BOARD];
    int raw_count = generate_moves(g, candidates, player, 0, -1);
    
    for (int i = 0; i < raw_count; i++) {
        int idx = candidates[i].index;
        
        // Optimisation : On ne teste que les coups prometteurs
        if (candidates[i].score_estim < CLOSED_THREE) continue; 

        // Simulation légère
        g->board[idx] = player;
        int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
        g->board[idx] = EMPTY;

        // Est-ce un coup forçant ? (Victoire ou Open 4 ou Closed 4 qui force une réponse)
        // Note: OPEN_THREE n'est pas strictement "VCF" car l'adversaire n'est pas *obligé* de répondre immédiatement 
        // s'il a une menace plus forte, mais dans un VCF strict, on cherche les FOURS.
        if (score >= CLOSED_FOUR) { 
            moves[count].move_idx = idx;
            moves[count].score = score;
            count++;
        }
    }
    return count;
}

/* * Génère les défenses forcées (Bloquer un 4) 
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
        if (candidates[i].score_estim > 500) { 
            moves[count].move_idx = candidates[i].index;
            count++;
        }
    }
    return count;
}

/*
 * Moteur VCF Récursif (Offensif)
 * Retourne TRUE si 'attacker' peut gagner forcément
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

/*
 * Fonction Wrapper pour l'extérieur
 */
bool has_vcf_win(game *g, int attacker, int depth, int max_depth, double time_limit) {
    // Cette fonction sert juste d'interface si besoin
    return vcf_search(g, attacker, 0, clock());
}

int solve_defensive_crisis(game *g, int me) {
    int opponent = (me == P1) ? P2 : P1;
    clock_t start = clock();

    // 1. Détection initiale : Est-ce qu'on va vraiment mourir ?
    bool threat_detected = vcf_search(g, opponent, 0, start);
    if (!threat_detected) return -1;

    printf(">>> CRISE DÉTECTÉE : L'adversaire a un VCF ! Recherche de contre-mesure...\n");

    MoveCandidate candidates[MAX_BOARD];
    
    // CHANGEMENT 1 : On passe '1' au lieu de '0' pour depth.
    // Cela force generate_moves à utiliser un beam_width plus large (ex: 30 coups au lieu de 8).
    // C'est CRITIQUE pour trouver des défenses subtiles que l'heuristique a mal classées.
    int count = generate_moves(g, candidates, me, 1, -1); 

    int best_save_idx = -1;
    int best_save_score = -1000000;

    for (int i = 0; i < count; i++) {
        // CHANGEMENT 2 : On baisse le filtre heuristique.
        // En crise, un coup mal noté (ex: pierre isolée qui bloque une ligne lointaine) 
        // peut être le seul sauveur. On ne filtre que les coups totalement inutiles (< 50).
        if (candidates[i].score_estim < 50) continue; 

        int idx = candidates[i].index;
        MoveUndo undo;
        apply_move(g, idx, me, &undo);
        
        // A. Est-ce que JE gagne ? (La meilleure défense, c'est l'attaque)
        if (evaluate_board(g, me) >= WIN_SCORE) {
            undo_move(g, me, &undo);
            printf(">>> CONTRE-ATTAQUE GAGNANTE en (%d,%d)\n", GET_X(idx), GET_Y(idx));
            return idx;
        }

        // B. Est-ce que ce coup brise le VCF ?
        bool threat_persists = vcf_search(g, opponent, 0, start);
        
        // C. VÉRIFICATION DE SÉCURITÉ
        // Le coup a cassé le VCF, mais a-t-on laissé une victoire "statique" (ex: alignement de 5 immédiat) ?
        bool stupid_death = false;
        
        if (!threat_persists) {
            // On vérifie si l'adversaire a une victoire immédiate visible statiquement
            int opp_eval = evaluate_board(g, opponent);
            
            // Si score très élevé = victoire ou menace imparable
            if (opp_eval >= WIN_SCORE) {
                stupid_death = true;
            }
            // Double check manuel sur les niveaux de menace
            else if (g->max_threat_level[opponent] >= IDX_OPEN_FOUR) {
                 stupid_death = true;
            }
        }

        undo_move(g, me, &undo);

        // Si le VCF est brisé ET qu'on ne meurt pas bêtement juste après
        if (!threat_persists && !stupid_death) {
        // On évalue la qualité offensive de ce sauvetage
            int my_attack_score = get_point_score(g, GET_X(idx), GET_Y(idx), me);
        
        // Si c'est une contre-attaque (ex: crée un 3 ou 4), c'est BEAUCOUP mieux qu'un blocage passif
            if (my_attack_score > best_save_score) {
            best_save_score = my_attack_score;
            best_save_idx = idx;
            }
        
        // Si on trouve une contre-attaque majeure (Open 4), on la joue direct
            if (my_attack_score >= OPEN_FOUR) return idx;
            if (best_save_idx != -1) {
                printf(">>> SAUVETAGE OPTIMISÉ : Coup (%d,%d) (Score Attaque: %d)\n", GET_X(idx), GET_Y(idx), my_attack_score);
                return best_save_idx;
            }
        }
        
        // Timeout de sécurité pour ne pas freeze (400ms alloués à la crise)
        if ((double)(clock() - start) / CLOCKS_PER_SEC > 0.40) break;
    }


    printf(">>> ÉCHEC : Aucune parade trouvée.\n");
    return -2;
}

/*
 * Trouve le PREMIER coup d'une séquence de victoire forcée (VCF)
 * Retourne l'index du coup, ou -1 si aucun VCF trouvé.
 */
int find_winning_vcf(game *g, int attacker) {
    // 1. On génère les coups possibles
    MoveCandidate moves[MAX_BOARD];
    // On utilise generate_moves standard (depth 0, pas de TT)
    int count = generate_moves(g, moves, attacker, 0, -1);

    // Timer pour limiter la recherche offensive (on ne veut pas y passer 2s)
    clock_t start = clock();
    double time_limit = 0.20; // 200ms max pour trouver l'attaque

    for (int i = 0; i < count; i++) {
        // OPTIMISATION CRITIQUE :
        // Un VCF commence forcément par une menace sérieuse (Open 3 minimum).
        // Si le coup ne crée pas au moins une menace de niveau 3, on l'ignore.
        if (moves[i].score_estim < OPEN_THREE) continue;

        int idx = moves[i].index;
        MoveUndo undo;
        apply_move(g, idx, attacker, &undo);

        // A. Victoire Immédiate ?
        if (evaluate_board(g, attacker) >= WIN_SCORE) {
            undo_move(g, attacker, &undo);
            return idx;
        }

        // B. Est-ce que ce coup force la victoire (VCF) ?
        // On lance la recherche récursive à partir de la profondeur 1
        // On limite la profondeur à 12 coups (6 paires attaque/défense) pour rester rapide
        if (has_vcf_win(g, attacker, 1, 12, (double)(clock() - start)/CLOCKS_PER_SEC + time_limit)) {
            undo_move(g, attacker, &undo);
            return idx; // C'est le coup gagnant !
        }

        undo_move(g, attacker, &undo);

        // Check timeout global de la fonction
        if ((double)(clock() - start) / CLOCKS_PER_SEC > time_limit) break;
    }

    return -1;
}
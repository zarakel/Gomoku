# Documentation Technique Approfondie du Moteur Gomoku

Ce document détaille l'architecture interne du moteur d'IA. Il ne s'agit pas d'un simple algorithme récursif, mais d'une architecture complexe inspirée des moteurs d'échecs modernes, conçue pour contourner l'explosion combinatoire inhérente au jeu de Gomoku (facteur de branchement ~200 coups possibles par tour).

---

## Table des Matières

1. Vue d'Ensemble de l'Architecture
2. Minimax et Alpha-Beta Pruning
3. Principal Variation Search (PVS)
4. Iterative Deepening et Aspiration Windows
5. Ordonnancement des Coups (Move Ordering)
6. Transposition Table et Zobrist Hashing
7. Late Move Reduction (LMR)
8. Système VCF (Victory by Continuous Four)
9. Heuristique d'Évaluation
10. Détection des Patterns Gappés
11. Équilibre Attaque/Défense
12. Gestion Mémoire et Optimisations Bas Niveau
13. Règles Spéciales : Double-Three et Captures
14. Flux d'Exécution Complet
15. Analyse des Performances

---

## 1. Vue d'Ensemble de l'Architecture

### 1.1 Fichiers et Responsabilités

| Fichier | Responsabilité |
|---------|----------------|
| ai.c | Orchestration principale, Iterative Deepening, Aspiration Windows |
| ai_search.c | Minimax, Alpha-Beta, PVS, VCF Solver, Logique défensive |
| ai_moves.c | Génération et tri des coups, Beam Search |
| ai_logic.c | Application/Annulation des coups (apply_move/undo_move) |
| ai_data.c | Zobrist Tables, Transposition Table, Killer Moves, History Heuristic |
| heuristics.c | Évaluation statique, Détection de patterns, Double-Three |

### 1.2 Flux de Décision Simplifié

```
makeIaMove()
    │
    ├─► solve_vcf()          [Victoire immédiate ? Menace à bloquer ?]
    │       │
    │       ├─► Victoire IA → Jouer
    │       ├─► Victoire Adverse → Bloquer
    │       ├─► Gapped Four/Three → Bloquer le trou
    │       ├─► Open Three adverse → Coup mixte ou blocage
    │       └─► Rien de critique → return -1
    │
    └─► run_iterative_deepening()  [Si VCF n'a rien trouvé]
            │
            └─► run_aspiration_search() pour depth = 2, 4, 6, ...
                    │
                    └─► minimax() avec PVS + LMR
```

### 1.3 Constantes Critiques

```c
#define WIN_SCORE       1000000000  // 10^9 - Victoire absolue
#define OPEN_FOUR       100000000   // 10^8 - Victoire au prochain tour
#define CLOSED_FOUR     10000000    // 10^7 - Force le blocage immédiat
#define OPEN_THREE      1000000     // 10^6 - Menace critique
#define CLOSED_THREE    100000      // 10^5 - Menace sérieuse
#define OPEN_TWO        10000       // 10^4 - Bon développement
#define CLOSED_TWO      1000        // 10^3 - Développement faible

#define TIME_LIMIT_MS   450         // Budget temps par coup
#define MAX_DEPTH       30          // Profondeur maximale théorique
#define TT_SIZE         (1 << 20)   // ~1 Million d'entrées TT
```

**Hiérarchie Logarithmique :** Chaque niveau est ~10x plus important que le précédent. Cela garantit qu'un OPEN_FOUR (victoire garantie) sera TOUJOURS prioritaire sur n'importe quelle combinaison de CLOSED_THREE.

---

## 2. Minimax et Alpha-Beta Pruning

### 2.1 Le Concept Fondamental

Minimax est un algorithme de théorie des jeux qui suppose que les deux joueurs jouent de manière optimale :
- **MAX** (IA) cherche à **maximiser** son score
- **MIN** (Adversaire) cherche à **minimiser** le score de MAX

L'arbre de jeu est exploré récursivement. À chaque nœud terminal (profondeur 0 ou victoire), on évalue la position.

### 2.2 Alpha-Beta : L'Élagage Intelligent

Alpha-Beta n'est pas un algorithme différent, c'est une **optimisation** du Minimax qui évite de calculer des branches inutiles.

#### Les Deux Variables Magiques

| Variable | Rôle | Phrase Clé |
|----------|------|------------|
| **Alpha** | Meilleur score garanti pour MAX | "Je peux avoir au moins α" |
| **Beta** | Meilleur score garanti pour MIN | "L'adversaire ne me laissera pas avoir plus que β" |

#### Règles de Coupure

```
┌─────────────────────────────────────────────────────────────┐
│  Score < Alpha  →  "Fail Low"   →  Inutile pour MAX         │
│  Score > Beta   →  "Fail High"  →  L'adversaire empêchera   │
│  Alpha >= Beta  →  "Cutoff"     →  Branche entière ignorée  │
└─────────────────────────────────────────────────────────────┘
```

#### Implémentation dans `minimax()`

```c
int minimax(game *g, int depth, int alpha, int beta, bool maximizingPlayer, ...) {
    // ... (TT probe, timeout check, terminal check)
    
    for (int i = 0; i < move_count; i++) {
        apply_move(g, idx, current_player, &undo);
        int val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ...);
        undo_move(g, current_player, &undo);
        
        if (maximizingPlayer) {
            if (val > best_val) best_val = val;
            if (val > alpha) alpha = val;       // Mise à jour du plancher
            if (alpha >= beta) {                // CUTOFF !
                debug_cutoff_count++;
                return best_val;                // On arrête d'explorer
            }
        } else {
            if (val < best_val) best_val = val;
            if (val < beta) beta = val;         // Mise à jour du plafond
            if (alpha >= beta) {                // CUTOFF !
                debug_cutoff_count++;
                return best_val;
            }
        }
    }
}
```

#### Visualisation d'un Cutoff

```
         MAX (IA)
        /    \
       /      \
    MIN        MIN
   /   \      /   \
  3     5    2     ?
       ↑          ↑
   Alpha=5    Beta=2 (hérité de MIN parent)
              
   → Le "?" n'est JAMAIS calculé car Alpha(5) >= Beta(2)
   → MAX sait qu'il a déjà mieux (5) que ce que MIN lui donnerait ici (≤2)
```

---

## 3. Principal Variation Search (PVS)

### 3.1 Le Concept

PVS (aussi appelé NegaScout) est une amélioration de l'Alpha-Beta basée sur l'hypothèse suivante :

> "Si mes coups sont bien triés, le premier coup testé est probablement le meilleur."

Pour tous les coups suivants, au lieu de chercher leur valeur exacte, on pose une **question binaire** : "Ce coup est-il meilleur que le premier ?"

### 3.2 La Null Window (Fenêtre Nulle)

| Type | Fenêtre | Objectif |
|------|---------|----------|
| Standard | `[Alpha, Beta]` | Trouver le score exact |
| Null Window | `[Alpha, Alpha+1]` | Juste savoir si > Alpha |

La Null Window provoque énormément de cutoffs car elle est extrêmement étroite.

### 3.3 Implémentation

```c
for (int i = 0; i < move_count; i++) {
    int idx = moves[i].index;
    apply_move(g, idx, current_player, &undo);
    
    int val;
    if (i == 0) {
        // ══════════════════════════════════════════════
        // PREMIER COUP (PV-Node) : Recherche complète
        // ══════════════════════════════════════════════
        val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ...);
    } else {
        // ══════════════════════════════════════════════
        // COUPS SUIVANTS : Null Window Scout
        // ══════════════════════════════════════════════
        if (maximizingPlayer) {
            val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, ...);
        } else {
            val = minimax(g, depth - 1 - reduction, beta - 1, beta, ...);
        }
        
        // RE-SEARCH si le scout trouve quelque chose de mieux
        if (reduction > 0 && val > alpha && val < beta) {
            val = minimax(g, depth - 1, alpha, beta, !maximizingPlayer, ...);
        }
    }
    
    undo_move(g, current_player, &undo);
    // ... (mise à jour alpha/beta)
}
```

### 3.4 Scénarios de la Null Window

```
Cas 1: val <= Alpha  →  "Fail Low"
       Le coup est PIRE que ce qu'on a déjà.
       → Pas de re-search, on passe au suivant.

Cas 2: val >= Alpha+1  →  "Fail High"  
       Le coup est MEILLEUR que prévu !
       → On relance une recherche complète [Alpha, Beta]

Cas 3: val == Alpha (exactement)
       → Égalité, pas d'amélioration, on continue.
```

**Gain :** En moyenne, 80% des coups sont "Fail Low" et ne nécessitent pas de re-search.

---

## 4. Iterative Deepening et Aspiration Windows

### 4.1 Iterative Deepening

Au lieu de lancer directement une recherche à profondeur 10, l'IA procède par étapes :

```
Depth 2  →  Score: -10000
Depth 4  →  Score: -3999000
Depth 6  →  Score: -399999000
...
Depth 30 →  Score: 1000000000 (VICTOIRE !)
```

#### Avantages

1. **Gestion du Temps :** Si le timeout approche, on garde le meilleur coup de la profondeur précédente.
2. **Move Ordering :** Le meilleur coup de Depth N-1 devient le premier coup testé à Depth N.
3. **Early Exit :** Si on trouve une victoire garantie, on s'arrête immédiatement.

#### Implémentation

```c
static int run_iterative_deepening(game *g, int ia_player, clock_t start) {
    int best_move = -1;
    int prev_score = 0;

    for (int depth = 2; depth <= MAX_DEPTH; depth += 2) {  // Incréments de 2
        int score = run_aspiration_search(g, depth, prev_score, &best_move, ...);
        
        if (score == -2) break;  // Timeout
        
        prev_score = score;
        
        if (score > WIN_SCORE - 5000) {
            // Victoire trouvée, inutile de chercher plus profond
            break;
        }
        
        if ((clock() - start) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) break;
    }
    return best_move;
}
```

**Note :** On incrémente de 2 car en Gomoku, l'horizon pair (après un coup de chaque joueur) est plus stable que l'horizon impair.

### 4.2 Aspiration Windows

#### Le Problème des Fenêtres Larges

Une recherche avec `[−∞, +∞]` explore beaucoup de branches inutiles car la fenêtre est trop large pour provoquer des cutoffs efficaces.

#### La Solution : Fenêtre Étroite

On suppose que le score de Depth N sera proche du score de Depth N-2 :

```c
int window = 500;
int alpha = prev_score - window;  // Ex: -10000 - 500 = -10500
int beta = prev_score + window;   // Ex: -10000 + 500 = -9500
```

#### Gestion des Échecs

Si le score sort de la fenêtre, on la **réouvre** :

```c
if (score < alpha || score > beta) {
    // "Aspiration Fail" - La supposition était fausse
    printf("Aspiration Fail at depth %d (Score %d outside [%d, %d])\n", ...);
    
    // On réouvre complètement
    alpha = INT_MIN;
    beta = INT_MAX;
    // Et on recommence cette profondeur
}
```

#### Visualisation

```
Depth 2:  prev_score = -10000
          Window = [-10500, -9500]
          
Depth 4:  Recherche dans [-10500, -9500]
          Score trouvé: -3999000  ← HORS FENÊTRE !
          
          "Aspiration Fail" → Re-search avec [-∞, +∞]
          Score confirmé: -3999000
          
Depth 6:  Window = [-3999500, -3998500]
          ...
```

---

## 5. Ordonnancement des Coups (Move Ordering)

### 5.1 Importance Critique

L'efficacité de l'Alpha-Beta dépend **entièrement** de l'ordre des coups :
- **Coups bien triés :** O(b^(d/2)) nœuds explorés
- **Coups mal triés :** O(b^d) nœuds explorés

Pour un facteur de branchement b=50 et profondeur d=10 :
- Bien trié : ~70 000 nœuds
- Mal trié : ~97 000 000 000 000 nœuds

### 5.2 Pipeline de Tri (5 Étapes)

```
┌────────────────────────────────────────────────────────────┐
│  1. HASH MOVE (TT)      │  Score: 2 100 000 000           │
│     Meilleur coup de    │  Le "VIP" absolu                │
│     la recherche        │                                  │
│     précédente          │                                  │
├────────────────────────────────────────────────────────────┤
│  2. VICTOIRES           │  Score: 2 000 000 000           │
│     Coups qui gagnent   │  (via quick_evaluate_move)      │
│     immédiatement       │                                  │
├────────────────────────────────────────────────────────────┤
│  3. KILLER MOVES        │  Score: 100 000 000 / 90 000 000│
│     Coups qui ont       │  Slot 0 > Slot 1                │
│     provoqué des        │                                  │
│     cutoffs à cette     │                                  │
│     profondeur          │                                  │
├────────────────────────────────────────────────────────────┤
│  4. HISTORY HEURISTIC   │  Score: Accumulé globalement    │
│     Coups              │  += depth² à chaque cutoff      │
│     historiquement bons│                                  │
├────────────────────────────────────────────────────────────┤
│  5. ÉVALUATION STATIQUE │  Score: quick_evaluate_move()   │
│     Analyse tactique    │  Attaque + Défense combinées    │
│     de chaque coup      │                                  │
└────────────────────────────────────────────────────────────┘
```

### 5.3 Killer Moves en Détail

#### Principe

Un "Killer Move" est un coup qui a provoqué un **Beta Cutoff** à la même profondeur dans une autre branche de l'arbre.

#### Logique au Gomoku

Les menaces sont souvent **indépendantes de la position exacte** des autres pierres éloignées. Si le coup (10,10) réfute une attaque adverse dans la variation A, il y a 90% de chances qu'il réfute aussi l'attaque dans la variation B.

#### Implémentation

```c
// Structure : killer_moves[depth][slot]
// 2 slots par profondeur (le plus récent écrase l'ancien)

// Lors d'un cutoff :
void update_killers(int depth, int move) {
    if (killer_moves[depth][0] != move) {
        killer_moves[depth][1] = killer_moves[depth][0];  // Décalage
        killer_moves[depth][0] = move;                     // Nouveau tueur
    }
}

// Lors du tri :
if (idx == killer_moves[depth][0]) score = 100000000;
else if (idx == killer_moves[depth][1]) score = 90000000;
```

### 5.4 History Heuristic en Détail

#### Différence avec Killer Moves

| Killer Moves | History Heuristic |
|--------------|-------------------|
| Liés à la profondeur | Global (toute la partie) |
| Éphémères | Persistant |
| 2 coups max par profondeur | Score pour chaque case |

#### Implémentation

```c
// Structure : history_heuristic[MAX_BOARD]
// Un compteur par case du plateau

// Lors d'un cutoff ou meilleur coup :
history_heuristic[move_idx] += depth * depth;  // Bonus quadratique

// Lors du tri :
score += history_heuristic[idx];
```

**Pourquoi depth² ?** Les coups trouvés à haute profondeur sont plus fiables, donc on leur donne plus de poids.

### 5.5 Quick Evaluate Move

Cette fonction évalue rapidement le potentiel tactique d'un coup :

```c
int quick_evaluate_move(game *g, int idx, int player) {
    int opponent = (player == P1) ? P2 : P1;
    int x = GET_X(idx);
    int y = GET_Y(idx);

    // 1. Score DÉFENSIF : "Que se passe-t-il si l'adversaire joue ici ?"
    g->board[idx] = opponent;
    int defense_score = get_point_score(g, x, y, opponent);
    g->board[idx] = EMPTY;

    // 2. Score OFFENSIF : "Que se passe-t-il si je joue ici ?"
    g->board[idx] = player; 
    int attack_score = get_point_score(g, x, y, player);
    g->board[idx] = EMPTY;

    // 3. HIÉRARCHIE avec priorité à l'attaque à niveau égal
    if (attack_score >= WIN_SCORE) return 2000000000;      // Victoire !
    if (defense_score >= WIN_SCORE) return 1950000000;     // Bloque défaite
    if (attack_score >= OPEN_FOUR) return 1900000000;      // Victoire garantie
    if (defense_score >= OPEN_FOUR) return 1850000000;     // Bloque victoire garantie
    // ... etc
    
    // 4. Bonus pour les coups polyvalents (attaque ET défense)
    if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
        return 1750000000;  // Coup idéal
    }
    
    return attack_score + (defense_score / 2);  // Léger biais offensif
}
```

---

## 6. Transposition Table et Zobrist Hashing

### 6.1 Le Problème des Transpositions

Au Gomoku, différents ordres de coups peuvent mener à la **même position** :

```
Séquence A: (9,9) → (10,10) → (8,8)
Séquence B: (8,8) → (9,9) → (10,10)
            ↓
        Même plateau final !
```

Sans mémorisation, on recalcule ces positions des milliers de fois.

### 6.2 Zobrist Hashing

#### Concept

Chaque position du plateau possède une **signature unique de 64 bits**. Cette signature est calculable de manière **incrémentale** (sans re-scanner tout le plateau).

#### La Table Zobrist

```c
uint64_t zobrist_table[MAX_BOARD][3];  // [case][valeur]
// zobrist_table[i][0] = nombre aléatoire pour "case i vide"
// zobrist_table[i][P1] = nombre aléatoire pour "case i = Joueur 1"
// zobrist_table[i][P2] = nombre aléatoire pour "case i = Joueur 2"
```

#### Initialisation (une seule fois au démarrage)

```c
void init_zobrist() {
    for (int i = 0; i < MAX_BOARD; i++) {
        zobrist_table[i][0] = rand64();   // 64 bits aléatoires
        zobrist_table[i][P1] = rand64();
        zobrist_table[i][P2] = rand64();
    }
}
```

#### Mise à Jour Incrémentale

La magie du XOR : `A ^ A = 0` et `A ^ 0 = A`

```c
// Quand on pose une pierre :
g->current_hash ^= zobrist_table[idx][player];

// Quand on retire une pierre (capture) :
g->current_hash ^= zobrist_table[idx][opponent];  // Retire
// (La case devient vide, mais on n'a pas besoin de XOR avec 0)
```

**Complexité :** O(1) par coup, au lieu de O(361) si on recalculait tout.

### 6.3 Structure de la Transposition Table

```c
typedef struct {
    uint64_t key;       // Signature complète (pour détecter les collisions)
    int depth;          // À quelle profondeur on a calculé
    int value;          // Le score trouvé
    int flag;           // Type : EXACT, LOWERBOUND, UPPERBOUND
    int best_move;      // Le meilleur coup pour cette position
} TTEntry;

TTEntry transposition_table[TT_SIZE];  // ~1 Million d'entrées
```

### 6.4 Types de Scores (Flags)

| Flag | Signification | Quand ? |
|------|---------------|---------|
| `TT_EXACT` | Score exact | Pas de cutoff, recherche complète |
| `TT_LOWERBOUND` | Score ≥ value | Beta cutoff (on a trouvé mieux) |
| `TT_UPPERBOUND` | Score ≤ value | Alpha cutoff (l'adversaire empêche) |

### 6.5 Utilisation dans Minimax

#### Probe (Lecture)

```c
TTEntry *entry = tt_probe(g->current_hash);
if (entry != NULL && entry->depth >= depth) {
    if (entry->flag == TT_EXACT) {
        return entry->value;  // Score exact réutilisable !
    }
    else if (entry->flag == TT_LOWERBOUND) {
        if (entry->value > alpha) alpha = entry->value;  // Relève le plancher
    }
    else if (entry->flag == TT_UPPERBOUND) {
        if (entry->value < beta) beta = entry->value;    // Abaisse le plafond
    }
    
    if (alpha >= beta) {
        return entry->value;  // Cutoff grâce à la TT !
    }
}
```

#### Save (Écriture)

```c
void tt_save(uint64_t key, int depth, int val, int flag, int best_move) {
    int idx = key % TT_SIZE;  // Modulo rapide (TT_SIZE est puissance de 2)
    
    // Remplacement si : nouvelle entrée OU profondeur supérieure
    if (transposition_table[idx].key != key || 
        depth >= transposition_table[idx].depth) {
        transposition_table[idx].key = key;
        transposition_table[idx].depth = depth;
        transposition_table[idx].value = val;
        transposition_table[idx].flag = flag;
        transposition_table[idx].best_move = best_move;
    }
}
```

---

## 7. Late Move Reduction (LMR)

### 7.1 Le Concept

Si les coups sont bien triés, les coups en **fin de liste** sont probablement mauvais. Au lieu de les explorer à pleine profondeur, on les "survole" avec une **profondeur réduite**.

### 7.2 Conditions d'Application

```c
int reduction = 0;
if (depth >= 4 && i >= 4) reduction = 1;   // Coups 5+ à depth 4+ : -1
if (depth >= 6 && i >= 8) reduction = 2;   // Coups 9+ à depth 6+ : -2
```

| Profondeur | Position du coup | Réduction |
|------------|------------------|-----------|
| depth < 4 | Tous | 0 |
| depth 4-5 | Coups 1-4 | 0 |
| depth 4-5 | Coups 5+ | 1 |
| depth 6+ | Coups 1-4 | 0 |
| depth 6+ | Coups 5-8 | 1 |
| depth 6+ | Coups 9+ | 2 |

### 7.3 Le Filet de Sécurité (Re-Search)

Si un coup réduit donne un score **meilleur que prévu**, on admet l'erreur et on relance à pleine profondeur :

```c
// Recherche réduite
val = minimax(g, depth - 1 - reduction, alpha, alpha + 1, ...);

// Si le score est étonnamment bon...
if (reduction > 0 && val > alpha && val < beta) {
    // Re-search à pleine profondeur
    val = minimax(g, depth - 1, alpha, beta, ...);
}
```

### 7.4 Impact sur les Performances

| Sans LMR | Avec LMR |
|----------|----------|
| Depth 8 max (~10 000 nœuds) | Depth 10+ stable (~30 000 nœuds) |
| Timeout fréquent à Depth 10 | Depth 30 atteignable (positions simples) |

**Explication :** On "survole" ~70% des branches de l'arbre sans perdre en qualité tactique, car les coups importants (Killer, TT, menaces) sont testés en premier et ne subissent pas de réduction.

---

## 8. Système VCF (Victory by Continuous Four)

### 8.1 Objectif

Le VCF Solver répond à la question : "Y a-t-il une **victoire forcée** ou une **menace critique** à traiter immédiatement ?"

C'est une **pré-recherche rapide** avant le Minimax, qui permet de :
1. Gagner instantanément si possible
2. Bloquer une défaite imminente
3. Neutraliser les menaces dangereuses (Gapped Threes, Open Threes)

### 8.2 Hiérarchie des Priorités (solve_vcf)

```c
int solve_vcf(game *g, int ia_player, clock_t start_time) {
    int opponent = (ia_player == P1) ? P2 : P1;

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 0 : VICTOIRE IMMÉDIATE POUR L'IA
    // ═══════════════════════════════════════════════════════════
    int my_win = find_winning_move(g, ia_player);
    if (my_win != -1) return my_win;  // JE GAGNE !
    
    // Victoire par capture (5 paires)
    for (int i = 0; i < MAX_BOARD; i++) {
        // ... (vérifier si ce coup donne 5 captures)
        if (captures >= 5) return i;
    }

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 1 : BLOCAGE VICTOIRE ADVERSE
    // ═══════════════════════════════════════════════════════════
    int opp_win = find_winning_move(g, opponent);
    if (opp_win != -1) return opp_win;  // BLOQUER SA VICTOIRE !
    
    // Blocage victoire par capture adverse
    // ...

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 1.5 : GAPPED PATTERNS (Voir section 10)
    // ═══════════════════════════════════════════════════════════
    int opp_gapped_three = find_gapped_three_hole(g, opponent);
    // ...

    // ═══════════════════════════════════════════════════════════
    // ÉTAPES 2-7 : Logique d'équilibre (Voir section 11)
    // ═══════════════════════════════════════════════════════════
    // ...
    
    return -1;  // Rien de critique, passer au Minimax
}
```

### 8.3 Fonctions de Détection

#### find_winning_move

Scanne toutes les cases vides et vérifie si y jouer donne WIN_SCORE :

```c
static int find_winning_move(game *g, int player) {
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        int score = evaluate_move_with_captures_full(g, i, player);
        
        if (score >= WIN_SCORE) return i;  // VICTOIRE !
    }
    return -1;
}
```

#### evaluate_move_with_captures_full

Simule le coup **avec les captures** pour détecter les victoires "cachées" :

```c
static int evaluate_move_with_captures_full(game *g, int idx, int player) {
    MoveUndo undo;
    apply_move(g, idx, player, &undo);  // Pose + Captures
    
    int score = get_point_score(g, GET_X(idx), GET_Y(idx), player);
    
    // Bonus : Les captures peuvent ouvrir de nouvelles lignes
    if (undo.captured_count > 0) {
        // Scanner les voisins des cases libérées
        // ...
    }
    
    undo_move(g, player, &undo);
    return score;
}
```

---

## 9. Heuristique d'Évaluation

### 9.1 Vue d'Ensemble

L'évaluation se fait en deux niveaux :
1. **get_point_score** : Score d'une **case spécifique** (analyse locale)
2. **evaluate_board** : Score du **plateau entier** (somme + facteurs stratégiques)

### 9.2 evaluate_line : Le Scanner Rayon-X

Cette fonction analyse **une direction** depuis une case :

```c
static inline int evaluate_line(game *g, int x, int y, int dx, int dy, int player) {
    int len = 1;  // La pierre de départ
    bool start_open = false;
    bool end_open = false;
    
    // ═══════════════════════════════════════════════════════════
    // SCAN POSITIF (vers dx, dy)
    // ═══════════════════════════════════════════════════════════
    int i = 1;
    for (; i < 5; i++) {
        int nx = x + dx * i;
        int ny = y + dy * i;
        if (!IS_VALID(nx, ny)) break;
        
        int cell = g->board[GET_INDEX(nx, ny)];
        if (cell == player) len++;          // Même couleur → Continue
        else if (cell == EMPTY) { 
            end_open = true;                 // Bout ouvert !
            break; 
        }
        else break;                          // Adversaire → Bloqué
    }

    // ═══════════════════════════════════════════════════════════
    // SCAN NÉGATIF (vers -dx, -dy)
    // ═══════════════════════════════════════════════════════════
    int j = 1;
    for (; j < 5; j++) {
        int nx = x - dx * j;
        int ny = y - dy * j;
        if (!IS_VALID(nx, ny)) break;
        
        int cell = g->board[GET_INDEX(nx, ny)];
        if (cell == player) len++;
        else if (cell == EMPTY) { 
            start_open = true; 
            break; 
        }
        else break;
    }

    // ═══════════════════════════════════════════════════════════
    // SCORING
    // ═══════════════════════════════════════════════════════════
    int open_ends = (start_open ? 1 : 0) + (end_open ? 1 : 0);
    
    if (len >= 5) return WIN_SCORE;
    if (len == 4) {
        if (open_ends == 2) return OPEN_FOUR;      // .XXXX.  → Victoire garantie
        if (open_ends == 1) return CLOSED_FOUR;    // OXXXX.  → Force le blocage
    }
    if (len == 3) {
        if (open_ends == 2) return OPEN_THREE;     // .XXX.   → Menace critique
        if (open_ends == 1) return CLOSED_THREE;   // OXXX.   → Menace sérieuse
    }
    // ... (len == 2)
    
    // Détection des GAPPED PATTERNS (voir section 10)
    // ...
}
```

### 9.3 get_point_score : Les 4 Directions

```c
int get_point_score(game *g, int x, int y, int player) {
    int total_score = 0;
    
    total_score += evaluate_line(g, x, y, 1, 0, player);   // Horizontal →
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    
    total_score += evaluate_line(g, x, y, 0, 1, player);   // Vertical ↓
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    
    total_score += evaluate_line(g, x, y, 1, 1, player);   // Diagonale ↘
    if (total_score >= WIN_SCORE) return WIN_SCORE;
    
    total_score += evaluate_line(g, x, y, 1, -1, player);  // Diagonale ↗
    
    return total_score;
}
```

**Optimisation :** Early return dès qu'on atteint WIN_SCORE.

### 9.4 evaluate_board : La Vue Globale

```c
int evaluate_board(game *g, int player) {
    int opponent = (player == P1) ? P2 : P1;
    
    // 1. Scan de toutes les pierres
    int my_score, opp_score, my_max_threat, opp_max_threat;
    recalculate_scores(g, &my_score, &opp_score, &my_max_threat, &opp_max_threat);
    
    // 2. Victoire/Défaite absolue
    if (opp_max_threat >= WIN_SCORE) return -WIN_SCORE;  // Je perds
    if (my_max_threat >= WIN_SCORE) return WIN_SCORE;    // Je gagne
    
    // 3. Score de base
    long long final_score = my_score - opp_score;
    
    // 4. Panique Proportionnelle
    // Si l'adversaire a une menace >= OPEN_THREE et que je n'ai pas de réponse...
    if (opp_max_threat >= OPEN_THREE && my_max_threat < CLOSED_FOUR) {
        final_score -= opp_max_threat * 3;  // Pénalité sévère !
    }
    
    // 5. Captures
    final_score += g->captures[player] * CAPTURE_BONUS;
    final_score -= g->captures[opponent] * CAPTURE_BONUS;
    
    return (int)final_score;
}
```

### 9.5 Optimisation : Scan Unique par Ligne

Pour éviter de compter la même ligne plusieurs fois, on ne scanne que si la pierre est le **début** de la ligne :

```c
// Horizontal : seulement si pas de pierre à gauche
if (x == 0 || g->board[idx - 1] != player) {
    val = evaluate_line(g, x, y, 1, 0, player);
}

// Vertical : seulement si pas de pierre au-dessus
if (y == 0 || g->board[idx - BOARD_SIZE] != player) {
    val = evaluate_line(g, x, y, 0, 1, player);
}
// ... etc
```

---

## 10. Détection des Patterns Gappés

### 10.1 Le Problème

Un "Gapped Pattern" est un alignement avec un **trou** au milieu :

```
X_XXX   ← Gapped Four (4 pierres + 1 trou)
XX_XX   ← Gapped Four (symétrique)
.X_XX.  ← Gapped Open Three (3 pierres + 1 trou + 2 bords ouverts)
```

**Danger :** Ces patterns sont aussi mortels que leurs équivalents contigus !
- `X_XXX` = Victoire en 1 coup (remplir le trou)
- `.X_XX.` = Victoire en 2 coups (devient `.XXXX.` après le trou)

### 10.2 Détection dans evaluate_line

Après le scan des pierres contiguës, on cherche s'il y a des pierres **après le trou** :

```c
// Dans evaluate_line, après le scan positif :
if (end_open && i < 5) {
    int gap_x = x + dx * i;  // Position du trou
    int gap_y = y + dy * i;
    
    // Compter les pierres APRÈS le trou
    int extra_len = 0;
    for (int k = 1; k <= 4; k++) {
        int ppx = gap_x + dx * k;
        int ppy = gap_y + dy * k;
        if (!IS_VALID(ppx, ppy)) break;
        if (g->board[GET_INDEX(ppx, ppy)] == player) extra_len++;
        else break;
    }
    
    int virtual_len = len + extra_len;
    
    // Gapped Four = Victoire imminente
    if (virtual_len >= 4) {
        score = OPEN_FOUR;  // Traiter comme une victoire garantie
    }
}
```

### 10.3 Fonctions de Détection Spécialisées

#### find_gapped_four_hole

Trouve le **trou** d'un Gapped Four adverse (pour le bloquer) :

```c
int find_gapped_four_hole(game *g, int player) {
    // Pour chaque pierre du joueur...
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        // Pour chaque direction...
        for (int d = 0; d < 4; d++) {
            int hole = find_gap_in_line(g, x, y, dx[d], dy[d], player);
            if (hole != -1) return hole;  // Retourne l'INDEX du trou
        }
    }
    return -1;
}
```

#### find_gap_in_line

Cherche les patterns `X_XXX`, `XX_XX`, `XXX_X` dans une ligne :

```c
static int find_gap_in_line(game *g, int x, int y, int dx, int dy, int player) {
    // Buffer de 9 cases centré sur (x,y)
    int line[9], indices[9];
    for (int k = -4; k <= 4; k++) { /* ... remplir ... */ }
    
    // Pattern: X_XXX
    for (int start = 0; start <= 4; start++) {
        if (line[start] == player &&
            line[start + 1] == EMPTY &&       // ← Le trou !
            line[start + 2] == player &&
            line[start + 3] == player &&
            line[start + 4] == player) {
            return indices[start + 1];        // Retourne l'index du trou
        }
    }
    
    // Pattern: XX_XX (trou au milieu)
    // ...
    
    // Pattern: XXX_X
    // ...
}
```

#### find_gapped_three_hole

Similaire, mais pour les Gapped **Open** Threes (`.X_XX.` ou `.XX_X.`) :

```c
// Pattern: . X _ X X . (6 cases)
if (line[start] == EMPTY &&
    line[start + 1] == player &&
    line[start + 2] == EMPTY &&       // ← Le trou
    line[start + 3] == player &&
    line[start + 4] == player &&
    line[start + 5] == EMPTY) {
    return indices[start + 2];
}
```

### 10.4 Intégration dans solve_vcf

```c
// ÉTAPE 3.5 : Gapped Three adverse → BLOCAGE PRIORITAIRE
int opp_gapped_three = find_gapped_three_hole(g, opponent);
int opp_gapped_count = count_gapped_threes(g, opponent);

if (opp_gapped_three != -1 && opp_gapped_count > 0) {
    // Un Gapped Open Three est aussi dangereux qu'un Open Three !
    if (my_best_score < CLOSED_FOUR) {
        return opp_gapped_three;  // Bloquer le trou !
    }
}
```

---

## 11. Équilibre Attaque/Défense

### 11.1 Le Problème de la Défense Pure

Une IA qui ne fait que défendre finit par perdre contre un adversaire qui crée des **doubles menaces**.

### 11.2 Comptage des Menaces

```c
static int count_serious_threats(game *g, int player) {
    int threat_count = 0;
    
    for (int idx = 0; idx < MAX_BOARD; idx++) {
        if (g->board[idx] != player) continue;
        
        for (int d = 0; d < 4; d++) {
            // Compter pierres alignées + bouts ouverts
            int stones = ...;
            int open_ends = ...;
            
            // Menace sérieuse = 3+ pierres avec au moins 1 bout ouvert
            if (stones >= 3 && open_ends >= 1) {
                threat_count++;
            }
        }
    }
    return threat_count;
}
```

### 11.3 Coups Mixtes (Dual Purpose)

Un coup "mixte" est un coup qui **attaque ET défend** simultanément :

```c
static int find_best_dual_purpose_move(game *g, int ia_player, int opponent) {
    int best_idx = -1;
    int best_combined_score = 0;
    
    for (int i = 0; i < MAX_BOARD; i++) {
        if (g->board[i] != EMPTY) continue;
        
        // Score offensif
        g->board[i] = ia_player;
        int attack_score = get_point_score(g, ...);
        g->board[i] = EMPTY;
        
        // Score défensif
        g->board[i] = opponent;
        int defense_score = get_point_score(g, ...);
        g->board[i] = EMPTY;
        
        // Bonus massif pour les coups polyvalents
        int combined = 0;
        if (attack_score >= OPEN_THREE && defense_score >= OPEN_THREE) {
            combined = attack_score + defense_score + 50000000;  // GROS BONUS
        }
        // ...
        
        if (combined > best_combined_score) {
            best_combined_score = combined;
            best_idx = i;
        }
    }
    return best_idx;
}
```

### 11.4 Logique de Décision Complète

```c
// Dans solve_vcf :

// CAS 1 : Adversaire a CLOSED_FOUR → BLOCAGE OBLIGATOIRE
// (Sauf si j'ai OPEN_FOUR = victoire garantie)
if (best_opp_score >= CLOSED_FOUR) {
    if (my_best_score >= OPEN_FOUR) return my_best_idx;  // Je gagne quand même !
    return find_blocking_move(g, opponent);
}

// CAS 2 : J'ai OPEN_FOUR → VICTOIRE
if (my_best_score >= OPEN_FOUR) return my_best_idx;

// CAS 3 : J'ai CLOSED_FOUR → ATTAQUE (force la réponse)
if (my_best_score >= CLOSED_FOUR) return my_best_idx;

// CAS 3.5 : Gapped Three adverse → BLOCAGE PRIORITAIRE
if (opp_gapped_three != -1) return opp_gapped_three;

// CAS 4 : Adversaire a OPEN_THREE
if (best_opp_score >= OPEN_THREE) {
    // Si j'ai aussi un OPEN_THREE, chercher un coup mixte
    if (my_best_score >= OPEN_THREE) {
        int dual = find_best_dual_purpose_move(g, ia_player, opponent);
        if (dual != -1 && /* vérifie qualité */) return dual;
        
        // Si j'ai plus de menaces, ATTAQUER
        if (my_threats > opp_threats && opp_gapped_count == 0) {
            return my_best_idx;  // Contre-attaque !
        }
    }
    return find_blocking_move(g, opponent);
}

// CAS 5 : J'ai OPEN_THREE → DÉVELOPPER
if (my_best_score >= OPEN_THREE) return my_best_idx;

// CAS 6 : Rien de critique → MINIMAX
return -1;
```

---

## 12. Gestion Mémoire et Optimisations Bas Niveau

### 12.1 Représentation 1D du Plateau

```c
int board[361];  // Au lieu de board[19][19]
```

**Avantages :**
1. **Cache Locality :** Les cases adjacentes sont contiguës en RAM.
2. **Accès simplifié :** `board[idx]` au lieu de `board[y][x]`.
3. **Prefetching :** Le CPU précharge automatiquement les cases suivantes.

### 12.2 Macros de Conversion

```c
#define GET_INDEX(x, y) ((y) * BOARD_SIZE + (x))
#define GET_X(index) ((index) % BOARD_SIZE)
#define GET_Y(index) ((index) / BOARD_SIZE)
#define IS_VALID(x, y) ((x) >= 0 && (x) < BOARD_SIZE && (y) >= 0 && (y) < BOARD_SIZE)
```

**Pourquoi des macros ?** Pas d'overhead d'appel de fonction, le compilateur les inline.

### 12.3 Bounding Box Dynamique

Au lieu de scanner les 361 cases, on calcule une **zone active** :

```c
int min_x = BOARD_SIZE, max_x = 0, min_y = BOARD_SIZE, max_y = 0;

for (int i = 0; i < MAX_BOARD; i++) {
    if (g->board[i] != EMPTY) {
        int cx = GET_X(i), cy = GET_Y(i);
        if (cx < min_x) min_x = cx;
        if (cx > max_x) max_x = cx;
        // ...
    }
}

// Ajouter une marge de 2 cases
min_x = max(0, min_x - 2);
max_x = min(BOARD_SIZE - 1, max_x + 2);
// ...

// Ne scanner que cette zone
for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
        // ...
    }
}
```

### 12.4 Apply/Undo Move

Au lieu de copier tout le plateau, on **enregistre les modifications** :

```c
typedef struct {
    int move_idx;               // Où a-t-on joué ?
    int captured_indices[10];   // Quels pions capturés ?
    int captured_count;
    int prev_captures[3];       // Compteurs avant le coup
} MoveUndo;

void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    // Sauvegarder l'état
    undo->move_idx = idx;
    undo->prev_captures[P1] = g->captures[P1];
    undo->prev_captures[P2] = g->captures[P2];
    undo->captured_count = 0;
    
    // Poser la pierre
    g->board[idx] = player;
    g->current_hash ^= zobrist_table[idx][player];
    
    // Détecter et appliquer les captures
    // ... (enregistrer dans undo->captured_indices)
}

void undo_move(game *g, int player, MoveUndo *undo) {
    // Restaurer les captures
    for (int i = 0; i < undo->captured_count; i++) {
        g->board[undo->captured_indices[i]] = opponent;
        g->current_hash ^= zobrist_table[...][opponent];
    }
    
    // Retirer notre pierre
    g->board[undo->move_idx] = EMPTY;
    g->current_hash ^= zobrist_table[undo->move_idx][player];
    
    // Restaurer les compteurs
    g->captures[P1] = undo->prev_captures[P1];
    g->captures[P2] = undo->prev_captures[P2];
}
```

**Complexité :** O(1) au lieu de O(361) pour copier le plateau.

### 12.5 Vérification de Timeout

Au lieu de vérifier à chaque nœud (coûteux), on vérifie tous les 2048 nœuds :

```c
if ((debug_node_count & 2047) == 0) {  // 2047 = 0b11111111111
    if ((clock() - start_time) * 1000 / CLOCKS_PER_SEC > TIME_LIMIT_MS) {
        return -2;  // Signal timeout
    }
}
```

**Pourquoi `& 2047` ?** C'est un modulo ultra-rapide (1 instruction CPU) car 2048 est une puissance de 2.

---

## 13. Règles Spéciales : Double-Three et Captures

### 13.1 Règle du Double-Three

Un coup est **interdit** s'il crée simultanément **deux Free Threes**.

#### Définition d'un Free Three

Un Free Three est un alignement de 3 pierres avec les deux bouts ouverts :

```
. X X X .   ← Free Three classique
. X . X X . ← Free Three avec trou (Gapped)
. X X . X . ← Free Three avec trou (Gapped)
```

#### Condition Critique

Le coup doit **faire partie** du Free Three créé. Si le Free Three existait déjà, il ne compte pas.

#### Implémentation

```c
int check_free_three_pattern(game *g, int x, int y, int dx, int dy, int player) {
    // Buffer de 13 cases centré sur le coup
    int line[13];
    // ... remplir avec simulation du coup ...
    
    // Pattern 1 : . X X X . (fenêtre de 5)
    // Le coup (center) doit être l'un des 3 X
    for (int start = 0; start <= 8; start++) {
        if (center < start + 1 || center > start + 3) continue;  // Coup hors pattern
        
        if (line[start] == EMPTY &&
            line[start + 1] == player &&
            line[start + 2] == player &&
            line[start + 3] == player &&
            line[start + 4] == EMPTY) {
            
            // Vérifier que ce n'est pas un Four déguisé
            if (/* pas de 4ème pierre adjacente */) {
                return 1;  // Free Three trouvé
            }
        }
    }
    // ... patterns avec trou ...
}

bool is_double_three(game *g, int idx, int player) {
    int free_threes = 0;
    
    for (int d = 0; d < 4; d++) {
        if (check_free_three_pattern(g, x, y, dx[d], dy[d], player)) {
            free_threes++;
            if (free_threes >= 2) return true;
        }
    }
    return false;
}
```

### 13.2 Système de Captures

#### Mécanisme

Pattern `A B B A` où A pose en extrémité → Les deux B sont capturés.

```
Avant:  . X O O .
Coup:   X X O O .   ← X joue à gauche
Après:  X . . . .   ← Les deux O sont capturés
```

#### Victoire par Capture

5 paires capturées = 10 pierres = Victoire.

#### Implémentation

```c
void apply_move(game *g, int idx, int player, MoveUndo *undo) {
    // ... pose de pierre ...
    
    // 8 directions à vérifier
    const int dirs[8][2] = {
        {1,0}, {0,1}, {-1,0}, {0,-1},
        {1,1}, {1,-1}, {-1,1}, {-1,-1}
    };
    
    for (int d = 0; d < 8; d++) {
        int x1 = x + dirs[d][0];     // Position 1
        int x2 = x + 2*dirs[d][0];   // Position 2
        int x3 = x + 3*dirs[d][0];   // Position 3
        // ... (idem pour y)
        
        // Pattern: PLAYER - OPP - OPP - PLAYER
        if (g->board[idx1] == opponent &&
            g->board[idx2] == opponent &&
            g->board[idx3] == player) {
            // Capture !
            undo->captured_indices[undo->captured_count++] = idx1;
            undo->captured_indices[undo->captured_count++] = idx2;
            g->board[idx1] = EMPTY;
            g->board[idx2] = EMPTY;
        }
    }
    
    g->captures[player] += undo->captured_count / 2;
}
```

---

## 14. Flux d'Exécution Complet

### 14.1 Diagramme de Séquence

```
makeIaMove()
│
├─► check_real_win(opponent)
│   └─► Si adversaire a déjà gagné → STOP
│
├─► solve_vcf()
│   │
│   ├─► find_winning_move(IA)
│   │   └─► Victoire immédiate ? → JOUER
│   │
│   ├─► find_winning_move(Adversaire)
│   │   └─► Adversaire gagne ? → BLOQUER
│   │
│   ├─► find_gapped_four_hole(Adversaire)
│   │   └─► Gapped Four adverse ? → BLOQUER LE TROU
│   │
│   ├─► find_gapped_three_hole(Adversaire)
│   │   └─► Gapped Three adverse ? → BLOQUER LE TROU
│   │
│   ├─► find_all_threats() pour les deux joueurs
│   │
│   ├─► Logique d'équilibre (voir section 11)
│   │
│   └─► return -1 (rien de critique)
│
├─► run_iterative_deepening()
│   │
│   └─► for depth = 2, 4, 6, ... MAX_DEPTH:
│       │
│       └─► run_aspiration_search(depth)
│           │
│           ├─► generate_moves()
│           │   │
│           │   ├─► Bounding Box
│           │   ├─► has_neighbors() filter
│           │   ├─► quick_evaluate_move() scoring
│           │   ├─► TT/Killer/History bonuses
│           │   └─► qsort() + Beam Search cutoff
│           │
│           └─► for each move:
│               │
│               └─► minimax(depth - 1)
│                   │
│                   ├─► TT Probe
│                   ├─► Timeout Check
│                   ├─► Terminal Check
│                   ├─► generate_moves()
│                   │
│                   └─► for each move:
│                       │
│                       ├─► is_double_three() → skip if true
│                       ├─► apply_move()
│                       │
│                       ├─► PVS Logic:
│                       │   ├─► i==0 → Full Window
│                       │   └─► i>0  → Null Window + LMR
│                       │
│                       ├─► undo_move()
│                       └─► Alpha-Beta Update
│
└─► finalize_move()
    │
    ├─► apply_move() définitif
    ├─► drawSquare() pour le coup
    ├─► drawSquare() pour les captures
    └─► Affichage temps/coordonnées
```
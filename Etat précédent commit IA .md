/*
 * ======================================================================================
 *                                  MOTEUR D'IA GOMOKU
 * ======================================================================================
 * 
 * ARCHITECTURE :
 * Ce moteur utilise une recherche arborescente Alpha-Beta optimisée par plusieurs
 * techniques modernes issues des moteurs d'échecs.
 *
 * 1. ALGORITHME PRINCIPAL : PVS (Principal Variation Search)
 *    - Variante du Minimax/Alpha-Beta.
 *    - Hypothèse : Le premier coup testé est souvent le meilleur (grâce au tri).
 *    - Optimisation : On cherche ce premier coup avec une fenêtre pleine (Alpha, Beta),
 *      et tous les suivants avec une "Null Window" (Alpha, Alpha+1) pour prouver qu'ils
 *      sont moins bons. Si la preuve échoue, on relance une recherche complète.
 *
 * 2. ORDONNANCEMENT DES COUPS (MOVE ORDERING) - CRITIQUE
 *    L'efficacité de l'élagage Alpha-Beta dépend à 100% de l'ordre des coups.
 *    Ordre de tri :
 *    a. Coup de la Transposition Table (Hash Move) : Le meilleur coup vu lors d'une recherche précédente.
 *    b. Coups de Capture / Victoire immédiate.
 *    c. Killer Moves : Coups qui ont provoqué un "Beta Cutoff" ailleurs à la même profondeur.
 *    d. History Heuristic : Coups qui ont souvent été bons historiquement dans la partie.
 *    e. Évaluation statique simple.
 *
 * 3. OPTIMISATIONS DE RECHERCHE
 *    - Transposition Table (Zobrist Hashing) : Mémoire cache pour ne pas recalculer les positions identiques.
 *    - Iterative Deepening : On cherche à Depth 1, puis 2, puis 3... Permet de gérer le temps
 *      et d'avoir toujours un "meilleur coup" prêt si le temps est écoulé.
 *    - Aspiration Windows : On suppose que le score sera proche du précédent. On cherche dans
 *      une fenêtre restreinte [Score-50, Score+50]. Si ça échoue, on élargit.
 *    - Late Move Reduction (LMR) : Si un coup est tardif dans la liste (donc supposé mauvais),
 *      on le recherche avec une profondeur réduite (Depth - 2). Gain de temps énorme.
 *
 * 4. HEURISTIQUE (DANS HEURISTICS.C)
 *    - Détection des motifs (Open 3, Broken 3, Broken 4).
 *    - "Peek" : Regarde au-delà des trous pour identifier les menaces cachées.
 *
 * ======================================================================================
 */

 Les résultats sont éloquents. Voici ce que l'on observe en comparant tes logs "Avant LMR" (Depth 8 max) et "Après LMR" (Depth 10 atteinte) :

Explosion de la Profondeur :

Avant : Tu atteignais péniblement Depth 8 avec ~10 000 nœuds, et Depth 10 timeoutait systématiquement.
Après : Tu atteins Depth 10 sur tous les coups (sauf 2 exceptions sur 15 coups). C'est un gain de performance massif.
Efficacité du LMR :

Regarde le nombre de nœuds à Depth 10 : ~25 000 à 40 000 nœuds.
Sans LMR, une Depth 10 complète aurait probablement demandé > 100 000 nœuds (estimation basée sur le facteur de branchement x5).
Le LMR a donc permis de "couper" environ 60% de l'arbre de recherche inutile, te permettant de rentrer dans les 0.5s.
Stabilité Tactique :

    La preuve : Regardez ce log : Aspiration Fail at depth 10 (Score -100000 outside [-500, 500]). Re-searching full window.

    Ce que ça veut dire : L'IA a tenté un calcul ultra-rapide (fenêtre minuscule). Elle a réalisé qu'elle allait perdre (-100,000, probablement un alignement adverse). Elle a relancé la recherche pour confirmer.

    Gain : Dans 90% des cas (les lignes sans "Fail"), l'IA a calculé la Depth 10 avec une fenêtre minuscule, gagnant un temps précieux.

C. Progression des Nœuds (Facteur de branchement effectif)

Regardons la croissance des nœuds sur un coup typique (IA plays at 9, 7) :

    Depth 4 : 281 nœuds

    Depth 6 : 2 140 nœuds (x7.6)

    Depth 8 : 14 674 nœuds (x6.8)

    Depth 10 : 75 223 nœuds (x5.1)

    Conclusion : Plus vous descendez profond, plus votre algorithme est efficace ! Le facteur de multiplication diminue. C'est le signe d'un Move Ordering (tri des coups) excellent (TT + History).

2. Stratégie Future : Comment passer de "Fort" à "Invincible" ?

Actuellement, votre IA joue très bien tactiquement (elle voit à 10 coups). Si vous voulez aller plus loin (battre des humains experts ou d'autres IA), augmenter la profondeur (Depth 12, 14...) avec la même méthode ne suffira plus (le Beam Search risque de couper le bon coup).

# Projet Gomoku AI

Bienvenue dans le projet **Gomoku AI**, un jeu de Gomoku (Ninuki-Renju) doté d'un moteur d'Intelligence Artificielle de pointe écrit en C, accompagné de plusieurs interfaces : une application de bureau native en C via la bibliothèque graphique **MLX42**.

Le moteur de jeu gère les règles spécifiques du Gomoku : alignement de 5 pierres, captures de paires, règle du double-trois (double-three) et détection de fin de partie par captures.

## Architecture Globale

Le projet repose sur une architecture découplée :

*   **Moteur C (Backend)** : Contient l'algorithme d'IA, la logique du jeu (règles et captures), le rendu de la fenêtre native en MLX42.


## Techniques Acquises & Utilisées

Ce projet a permis de concevoir un moteur d'IA hautement optimisé et une architecture réseau hybride en C. Voici les techniques phares implémentées :

### 1. Algorithmes de Recherche IA (Minimax / Negamax)
*   **Negamax avec Élagage Alpha-Beta (Alpha-Beta Pruning)** : Réduction drastique de l'espace de recherche en ignorant les branches de jeu sous-optimales.
*   **Approfondissement Itératif (Iterative Deepening)** : Recherche progressive de la profondeur (profondeurs 2, 4, 6, etc.) pour garantir d'avoir toujours le meilleur coup calculé dans le temps imparti (limite stricte à ~500ms). Comprend une parité sur les profondeurs pour éviter les oscillations d'évaluation.
*   **Principal Variation Search (PVS) & Fenêtre d'Aspiration (Aspiration Window)** : Optimisation des bornes d'Alpha-Beta en limitant les recherches aux variations principales et en effectuant des recherches à fenêtre nulle pour valider les coupes plus rapidement.
*   **Tactical Solver VCF (Victory by Continuous Fours)** : Résolveur tactique dédié qui recherche des séquences de coups forcés (menaces directes d'Open-4) pour tuer ou défendre instantanément (profondeur max de 30 coups/plies).
*   **Gestion de Crise (Crisis Mode)** : Analyse instantanée des menaces adverses (alignements imminents ou menaces de capture) afin de concentrer la recherche sur l'espace défensif pertinent.

### 2. Optimisations de Performance & Représentation
*   **Table de Transposition (Zobrist Hashing)** : Cache des états de plateau (~2 millions d'entrées) indexé par une clé de hachage Zobrist mise à jour de façon incrémentale. Permet d'éviter de réévaluer des positions déjà visitées (transpositions).
*   **Candidate Set Incrémental (cand_list)** : Maintien incrémental en $O(25)$ par coup/undo de la liste des cases candidates (les cases libres ayant des voisins dans un rayon de 2 cases), au lieu de rescanner tout le plateau de 19x19 ($O(361)$) à chaque étape. Cette optimisation fait gagner environ **+2 plies** de profondeur de recherche.
*   **Killer Moves & History Heuristic** : Ordonnancement dynamique des coups basé sur les heuristiques historiques et les coups perturbateurs ("killer moves") pour provoquer des coupes Alpha-Beta le plus tôt possible.
*   **Évaluation Heuristique Statique Fine** : Évaluation du plateau prenant en compte la centralité des pions, le nombre de captures en cours, et des bonus de structures (Open-Four, Closed-Four, Open-Three, etc.).

## Règles Spécifiques de Jeu (Standard 42)

*   **Plateau** : Grille de 19x19 intersections.
*   **Victoire par Alignement** : Aligner 5 pierres horizontalement, verticalement ou diagonalement.
*   **Victoire par Capture** : Capturer 5 paires adverses (10 pierres au total). Une capture a lieu lorsqu'un joueur encadre exactement une paire de pierres adverses adjacentes (ex : `X O O X`).
*   **Règle du Double-Trois (Double-Three)** : Il est interdit de poser une pierre créant simultanément deux alignements de trois pierres ouverts aux deux extrémités (double alignement de 3 libre), sauf si le coup génère une capture qui détruit cette configuration.

## Cheat Sheet d'Utilisation

### Compilation
Le projet utilise un `Makefile` pour compiler le backend C, télécharger et compiler MLX42.

| Commande | Action |
| :--- | :--- |
| `make` | Télécharge les dépendances, compile MLX42 et produit l'exécutable `gomoku`. |
| `make clean` | Supprime les fichiers objets intermédiaires (`.o`, `.d`). |
| `make fclean` | Nettoie tout (fichiers objets, exécutable et bibliothèques compilées). |
| `make re` | Recompile entièrement le projet depuis zéro. |
| `make docker` | Compile localement puis lance le conteneur Docker avec affichage X11. |

### Lancement Local Rapide

*   **Interface Graphique Native** : S'ouvre automatiquement sur votre bureau à l'exécution de `./gomoku`.

## Commandes de Jeu (Raccourcis)

### Sur l'interface Desktop Native (MLX42)
*   `CLIC GAUCHE` : Placer une pierre sur une case vide du plateau.
*   `CLIC GAUCHE` (sur le bouton **RESTART** en haut à droite) : Réinitialise la partie.
*   `ESPACE` : Active ou désactive le mode IA (l'IA jouera le rôle du Joueur 2 / Blancs).
*   `H` : Affiche un indice (**Hint**) en surbrillance jaune sur le plateau indiquant le meilleur coup calculé par l'IA à cet instant.
*   `ESC` : Ferme l'application.

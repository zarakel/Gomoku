# Gomoku AI Project

Welcome to the **Gomoku AI** project, a Gomoku (Ninuki-Renju) game powered by a state-of-the-art Artificial Intelligence engine written in C, accompanied by a native C desktop application built using the **MLX42** graphics library.

The game engine supports the specific rules of Gomoku: 5-in-a-row alignment, pair captures, the double-three rule, and win condition detection via captures.

## General Architecture

The project is built on a decoupled architecture:

*   **C Engine (Backend)**: Contains the AI algorithm, the game logic (rules and captures), and the rendering of the native window using MLX42.


## Technologies & Techniques Used

This project involved designing a highly optimized AI engine and a hybrid network architecture in C. Here are the main techniques implemented:

### 1. AI Search Algorithms (Minimax / Negamax)
*   **Negamax with Alpha-Beta Pruning**: Drastically reduces the search space by ignoring suboptimal game branches.
*   **Iterative Deepening**: Progressively searches deeper (depths 2, 4, 6, etc.) to guarantee that the best move is always computed within the allocated time limit (strictly capped at ~500ms). Includes depth parity checks to prevent evaluation oscillations.
*   **Principal Variation Search (PVS) & Aspiration Window**: Optimizes Alpha-Beta bounds by restricting searches to principal variations and performing null-window searches to trigger beta cuts faster.
*   **Victory by Continuous Fours (VCF) Tactical Solver**: A dedicated tactical solver that searches for sequences of forced moves (direct Open-4 threats) to win or defend instantly (up to a maximum depth of 30 plies).
*   **Crisis Mode**: Instant analysis of opponent threats (imminent alignments or capture threats) to concentrate the search space on the relevant defensive moves.

### 2. Performance Optimizations & Board Representation
*   **Transposition Table (Zobrist Hashing)**: A cache of board states (~2 million entries) indexed by a Zobrist hash key updated incrementally. Prevents re-evaluating already visited positions (transpositions).
*   **Incremental Candidate Set (cand_list)**: Incremental maintenance in $O(25)$ per move/undo of candidate squares (empty squares with neighbors within a 2-square radius), instead of rescanning the entire 19x19 board ($O(361)$) at each step. This optimization increases search depth by approximately **+2 plies**.
*   **Killer Moves & History Heuristic**: Dynamic move ordering based on historical heuristics and killer moves to trigger Alpha-Beta cutoffs as early as possible.
*   **Fine-grained Static Heuristic Evaluation**: Evaluates the board considering piece centrality, active captures, and structural bonuses (Open-Four, Closed-Four, Open-Three, etc.).

## Specific Game Rules (Standard 42)

*   **Board**: 19x19 grid of intersections.
*   **Victory by Alignment**: Align 5 stones horizontally, vertically, or diagonally.
*   **Victory by Capture**: Capture 5 opponent pairs (10 stones in total). A capture occurs when a player brackets exactly one adjacent pair of opponent stones (e.g., `X O O X`).
*   **Double-Three Rule**: It is forbidden to play a stone that simultaneously creates two open-ended three-in-a-row alignments (free double three), unless the move generates a capture that breaks this configuration.

## How to Use (Cheat Sheet)

### Compilation
The project uses a `Makefile` to compile the C backend and download & compile MLX42.

| Command | Action |
| :--- | :--- |
| `make` | Downloads dependencies, compiles MLX42, and produces the `gomoku` executable. |
| `make clean` | Removes intermediate object files (`.o`, `.d`). |
| `make fclean` | Cleans everything (object files, executable, and compiled libraries). |
| `make re` | Entirely rebuilds the project from scratch. |
| `make docker` | Builds locally and runs the Docker container with X11 display. |

### Quick Local Start

*   **Native Graphical Interface**: Opens automatically on your desktop when executing `./gomoku`.

## Game Controls (Shortcuts)

### Native Desktop Interface (MLX42)
*   `LEFT CLICK`: Place a stone on an empty board intersection.
*   `LEFT CLICK` (on the **RESTART** button at the top-right): Resets the game.
*   `SPACE`: Enables or disables AI mode (the AI will play as Player 2 / White).
*   `H`: Displays a hint highlighted in yellow on the board, showing the best move computed by the AI at that moment.
*   `ESC`: Closes the application.

NAME        := gomoku

# --- Directories ---
# Architecture définie : src/file/*.c et src/include
SRC_DIR     := file/src
INC_DIR     := file/include
OBJ_DIR     := obj
LIB_DIR     := lib
MLX_DIR     := $(LIB_DIR)/MLX42

# --- Compiler & Flags ---
CC          := cc
# Flags demandés (-Ofast -g -DDEBUG=1). 
# J'ajoute -MMD -MP pour la gestion automatique des dépendances (.h)
CFLAGS      := -Ofast -g -DDEBUG=1 -MMD -MP
CFLAGS      += -I$(INC_DIR) -I$(MLX_DIR)/include

# Pour réactiver les warnings stricts plus tard, décommente cette ligne :
# CFLAGS    += -Wextra -Wall -Werror

# --- Libraries ---
# Détection basique pour Linux vs Mac (Optional, based on your previous config)
LGLFW_PATH  := /usr/lib/x86_64-linux-gnu
LIBS        := $(MLX_DIR)/build/libmlx42.a -ldl -lglfw -pthread -lm -L$(LGLFW_PATH)

# --- Sources & Objects ---
# Trouve tous les .c dans src/file
SRCS        := $(shell find $(SRC_DIR) -iname "*.c")

# Crée la liste des objets dans obj/ en gardant la structure ou en aplanissant
# Ici, on remplace le chemin SRC_DIR par OBJ_DIR
OBJS        := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# --- Rules ---

all: libmlx $(NAME)

# Linkage final
$(NAME): $(OBJS)
	@$(CC) $(OBJS) $(LIBS) -o $(NAME)
	@printf "✅ Linked: $(NAME)\n"

# Compilation des objets
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
	@printf "🔨 Compiling: $(notdir $<)\n"

# Gestion de la MLX42
libmlx:
	@if [ ! -d "$(MLX_DIR)" ]; then \
		git clone https://github.com/codam-coding-college/MLX42.git $(MLX_DIR); \
	fi
	@cmake $(MLX_DIR) -B $(MLX_DIR)/build && make -C $(MLX_DIR)/build -j4

# Docker rule (conservée)
docker: all
	@cp $(NAME) ./file/$(NAME) 2>/dev/null || :
	docker compose up --build -d

# Nettoyage
clean:
	@rm -rf $(OBJ_DIR)
	@printf "🧹 Cleaned object files.\n"

fclean: clean
	@rm -f $(NAME)
	@rm -rf $(MLX_DIR)/build
	@printf "🗑️  Removed executable: $(NAME).\n"

re: fclean all

# Utils
git: fclean
	git add .
	git commit -m "auto commit"
	git push

brew:
	brew install glfw

# Inclusion des dépendances générées par -MMD
-include $(OBJS:.o=.d)

.PHONY: all clean fclean re libmlx git brew docker
NAME        := gomoku

# --- Directories ---
SRC_DIR     := file/src
INC_DIR     := file/include
OBJ_DIR     := obj
LIB_DIR     := lib
MLX_DIR     := $(LIB_DIR)/MLX42
MONGOOSE_DIR := $(LIB_DIR)/mongoose

# --- Compiler & Flags ---
CC          := cc
CFLAGS      := -Ofast -g -DDEBUG=1 -MMD -MP
CFLAGS      += -I$(INC_DIR) -I$(MLX_DIR)/include -I/usr/include/cjson

# Pour réactiver les warnings stricts plus tard, décommente cette ligne :
# CFLAGS    += -Wextra -Wall -Werror

# --- Libraries ---
LGLFW_PATH  := /usr/lib/x86_64-linux-gnu
LIBS        := $(MLX_DIR)/build/libmlx42.a $(MONGOOSE_DIR)/mongoose.o -ldl -lglfw -pthread -lm -lcurl -lcjson -L$(LGLFW_PATH)

# --- Sources & Objects ---
SRCS        := $(shell find $(SRC_DIR) -iname "*.c")
OBJS        := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# --- Rules ---

all: libmlx mongoose $(NAME)

# Linkage final
$(NAME): $(OBJS)
	@$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $(NAME)
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

# Gestion de Mongoose
mongoose:
	@if [ ! -d "$(MONGOOSE_DIR)" ]; then \
		git clone https://github.com/cesanta/mongoose.git $(MONGOOSE_DIR); \
	fi
	@cd $(MONGOOSE_DIR) && $(CC) -c mongoose.c -I. -o mongoose.o
	@printf "✅ Mongoose compiled\n"

# Docker rule
docker: all
	@cp $(NAME) ./file/$(NAME) 2>/dev/null || :
	docker compose up --build -d

# Nettoyage
clean:
	@rm -rf $(OBJ_DIR)
	@printf "🧹 Cleaned object files.\n"

fclean: clean
	@rm -f $(NAME)
	@rm -rf $(MLX_DIR)/build $(MONGOOSE_DIR)
	@printf "🗑️  Removed executable and libs.\n"

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

.PHONY: all clean fclean re libmlx mongoose git brew docker
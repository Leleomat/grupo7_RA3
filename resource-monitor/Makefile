# ============================================================
# Makefile - Resource Monitor (Control Group Manager)
# ============================================================

# Compilador e flags
CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Iinclude

# Diretórios
SRC_DIR := src
INC_DIR := include
OBJ_DIR := build
BIN_DIR := bin

# Nome do executável
TARGET := $(BIN_DIR)/resource-monitor

# Lista de arquivos-fonte e objetos
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))

# ============================================================
# Regras principais
# ============================================================

# Regra padrão
all: $(TARGET)

# Como gerar o binário final
$(TARGET): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "✅ Build concluído: $(TARGET)"

# Como gerar cada arquivo .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "🔧 Compilando: $<"

# ============================================================
# Regras auxiliares
# ============================================================

# Limpar arquivos compilados
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "🧹 Limpeza concluída."

# Rebuild total
rebuild: clean all

# Executar o programa (requer sudo para cgroups)
run: all
	sudo $(TARGET)

# ============================================================
# Fim do Makefile
# ============================================================
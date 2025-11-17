# ============================================================
# Makefile - Resource Monitor + Testes
# ============================================================

# Compilador e flags
CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -Iinclude

# Diretórios
SRC_DIR := src
INC_DIR := include
OBJ_DIR := build
BIN_DIR := bin
TEST_DIR := tests

# Nome do executável principal
TARGET := $(BIN_DIR)/resource-monitor

# Arquivos fonte e objetos do programa principal
SRC_FILES := $(wildcard $(SRC_DIR)/*.cpp)
OBJ_FILES := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRC_FILES))

# Arquivos de teste e executáveis correspondentes
TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_EXECUTABLES := $(patsubst $(TEST_DIR)/%.cpp,$(BIN_DIR)/%,$(TEST_SOURCES))

# ============================================================
# Regras principais
# ============================================================

all: $(TARGET) $(TEST_EXECUTABLES)

# Binário principal
$(TARGET): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "Build concluído: $(TARGET)"

# Arquivos .o do programa principal
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "Compilando: $<"

# ============================================================
# Compilação dos testes
# ============================================================

# Cada teste gera um executável próprio
$(BIN_DIR)/%: $(TEST_DIR)/%.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@
	@echo "Build do teste concluído: $@"

# ============================================================
# Regras auxiliares
# ============================================================

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Limpeza concluída."

rebuild: clean all

run: all
	sudo $(TARGET)

run-tests: all
	@for exe in $(TEST_EXECUTABLES); do \
		echo "Executando $$exe"; \
		./$$exe; \
	done

run-test-cpu: $(BIN_DIR)/test_cpu
	./$(BIN_DIR)/test_cpu

run-test-io: $(BIN_DIR)/test_io
	./$(BIN_DIR)/test_io

run-test-memory: $(BIN_DIR)/test_memory
	./$(BIN_DIR)/test_memory

# ============================================================
# Fim do Makefile
# ============================================================

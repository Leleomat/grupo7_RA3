

#include <atomic>            
#include <chrono>            
#include <cstring>           
#include <fstream>           
#include <iostream>          
#include <thread>            
#include <vector>            

std::atomic<bool> rodando{true}; // flag atômica para controlar execução

// função executada por cada thread que aloca e "toca" memória
void trabalhoMemoria(size_t tamanhoBytes, int id) {
    // aloca um vetor de bytes no heap do tamanho pedido
    std::vector<unsigned char> bloco;
    try {
        bloco.resize(tamanhoBytes); // tenta alocar
    } catch (...) {
        std::cerr << "Thread " << id << ": falha ao alocar " << tamanhoBytes << " bytes\n";
        return; // falha na alocação → termina a thread
    }

    // tamanho do passo para tocar cada página (4KB típico)
    size_t passo = 4096;
    while (rodando.load()) { // loop até a flag ser desligada
        for (size_t i = 0; i < tamanhoBytes; i += passo) {
            bloco[i] = static_cast<unsigned char>((i + static_cast<size_t>(id)) & 0xFF); // escreve um byte por página
            if (!rodando.load()) break; // checa flag dentro do loop para parada mais rápida
        }
        // pequena pausa para reduzir consumo de CPU absoluto e dar chance ao SO
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() {
    unsigned int numeroThreads = std::max(1u, std::thread::hardware_concurrency());
    // cada thread aloca o valor definido em ull
    size_t bytesPorThread = 100ull * 1024 * 1024;

    std::cout << "Teste Memória: " << numeroThreads << " threads, "
              << (bytesPorThread / (1024*1024)) << " MB por thread (aprox).\n";
    std::cout << "Pressione ENTER para parar.\n";

    std::vector<std::thread> threads; // guarda threads
    for (unsigned int i = 0; i < numeroThreads; ++i)
        threads.emplace_back(trabalhoMemoria, bytesPorThread, (int)i); // inicia threads

    std::cin.get();             // espera ENTER
    rodando.store(false);       // sinaliza parada

    for (auto &t : threads) if (t.joinable()) t.join(); // aguarda fim das threads
    std::cout << "Teste Memória finalizado.\n";
    return 0;
}

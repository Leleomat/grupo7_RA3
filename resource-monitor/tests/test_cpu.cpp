#include <atomic>            
#include <iostream>         
#include <thread>           
#include <vector>            
#include <cmath>             

std::atomic<bool> rodando{true}; // flag atômica compartilhada para controlar execução

// verifica se um número é primo (método simples)
bool ehPrimo(unsigned long n) {
    if (n < 2) return false;                // números < 2 não são primos
    if (n % 2 == 0) return n == 2;          // trata pares rapidamente
    unsigned long limite = (unsigned long)std::sqrt((double)n); // limite da busca
    for (unsigned long i = 3; i <= limite; i += 2) // testa apenas ímpares
        if (n % i == 0) return false;       // encontrou divisor → não é primo
    return true;                            // não encontrou divisores → primo
}

// função executada por cada thread para consumir CPU
void trabalhoCpu(int id) {
    unsigned long atual = static_cast<unsigned long>(2 + id);            // ponto de partida diferente por thread
    while (rodando.load()) {                 // loop até a flag ser false
        // mistura de trabalho: busca de primos + cálculos flutuantes
        if (ehPrimo(atual)) {                // se for primo, executa cálculo extra
            volatile double x = 0.0;         // volatile para evitar otimização agressiva
            for (int i = 0; i < 2000; ++i)   // laço para sobrecarregar FPU
                x += std::sin(i) * std::cos(i); // operações trigonométricas
            (void)x;                         // evita warning de variável não usada
        }
        // incrementa pulando por quantidade igual ao número de núcleos lógicos
        atual += std::thread::hardware_concurrency();
    }
}

int main() {
    unsigned int numeroThreads = std::max(1u, std::thread::hardware_concurrency());
    // número de threads = núcleos lógicos, mínimo 1
    std::cout << "Teste CPU: iniciando " << numeroThreads << " threads de cálculo.\n";
    std::cout << "Pressione ENTER para parar.\n";

    std::vector<std::thread> threads;        // container de threads
    for (unsigned int i = 0; i < numeroThreads; ++i)
        threads.emplace_back(trabalhoCpu, (int)i); // cria e inicia cada thread

    std::cin.get();                          // espera ENTER do usuário para parar
    rodando.store(false);                    // sinaliza parada para as threads

    for (auto &t : threads) if (t.joinable()) t.join(); // aguarda término de cada thread
    std::cout << "Teste CPU finalizado.\n";
    return 0;                                // retorna sucesso
}

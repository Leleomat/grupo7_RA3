// #include <iostream>
// #include <vector>
// #include <thread>
// #include <limits>
// #include <unistd.h> // getpid()

// int main() {
//     constexpr size_t MB = 1024 * 1024;
//     constexpr size_t allocSizeMB = 200; // 200 MB

//     std::cout << "Alocando " << allocSizeMB << " MB de memória...\n";

//     std::vector<char> memory;
//     try {
//         memory.resize(allocSizeMB * MB, 0);
//     } catch (const std::bad_alloc&) {
//         std::cerr << "Falha ao alocar memória.\n";
//         return 1;
//     }

//     // Acessa cada MB para realmente usar a memória
//     for (size_t i = 0; i < memory.size(); i += MB) {
//         memory[i] = 1;
//     }

//     std::cout << "Memória alocada e inicializada!\n";
//     std::cout << "Pressione ENTER para liberar memória e sair...\n";

//     std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
//     std::cin.get();

//     std::cout << "Liberando memória e saindo...\n";
//     return 0;
// }

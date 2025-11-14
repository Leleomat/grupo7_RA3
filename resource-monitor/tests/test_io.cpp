// #include <iostream>
// #include <fstream>
// #include <vector>
// #include <thread>
// #include <atomic>
// #include <chrono>
// #include <filesystem>

// const size_t CHUNK_SIZE = 1024 * 10; // 10 KB por leitura/escrita
// const int NUM_THREADS = 4;

// std::atomic<bool> running(true);

// void ioWorker(int id) {
//     std::vector<char> buffer(CHUNK_SIZE, 'A'); // dados de teste
//     while (running.load()) {
//         std::string filename = "temp_io_test_" + std::to_string(id) + ".tmp";

//         // Criar e escrever
//         {
//             std::ofstream out(filename, std::ios::binary);
//             if (out.is_open()) {
//                 out.write(buffer.data(), buffer.size());
//             }
//         }

//         // Ler
//         {
//             std::ifstream in(filename, std::ios::binary);
//             if (in.is_open()) {
//                 std::vector<char> readBuffer(CHUNK_SIZE);
//                 in.read(readBuffer.data(), readBuffer.size());
//             }
//         }

//         // Deletar
//         std::filesystem::remove(filename);

//         std::this_thread::sleep_for(std::chrono::milliseconds(10)); // leve pausa
//     }
// }

// int main() {
//     std::cout << "Iniciando I/O contínuo temporário com " << NUM_THREADS << " threads...\n";
//     std::cout << "Pressione ENTER para parar.\n";

//     std::vector<std::thread> threads;
//     for (int i = 0; i < NUM_THREADS; ++i) {
//         threads.emplace_back(ioWorker, i);
//     }

//     std::cin.get(); // espera Enter
//     running = false;

//     for (auto& t : threads) {
//         t.join();
//     }

//     std::cout << "I/O finalizado. Verifique rchar/wchar em /proc/[pid]/io\n";
//     return 0;
// }

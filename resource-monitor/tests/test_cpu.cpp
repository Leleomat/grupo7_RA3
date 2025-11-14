// #include <iostream>
// #include <thread>
// #include <vector>
// #include <cmath>
// #include <atomic>

// std::atomic<bool> run(true);

// void cpu_stress() {
//     double x = 0.0001;
//     while (run.load()) {
//         x = std::sin(x) * std::cos(x) * std::tan(x + 1.0);
//     }
// }

// int main() {
//     int threads = std::thread::hardware_concurrency();

//     std::cout << "Iniciando stress com " << threads
//               << " threads. Pressione ENTER para parar..." << std::endl;

//     std::vector<std::thread> workers;

//     workers.reserve(threads);

//     for (int i = 0; i < threads; i++)
//         workers.emplace_back(cpu_stress);

//     std::cin.get();  // espera ENTER
//     run.store(false);

//     for (auto &t : workers)
//         t.join();

//     std::cout << "Finalizado." << std::endl;
//     return 0;
// }

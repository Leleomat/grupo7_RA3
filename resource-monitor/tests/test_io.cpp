

// #include <atomic>            
// #include <chrono>            
// #include <fstream>           
// #include <iostream>         
// #include <string>            
// #include <thread>            
// #include <vector>            

// std::atomic<bool> rodando{true}; // flag de controle compartilhada

// long unsigned int buff = 100;
// // função que escreve continuamente no arquivo (append)
// void escritorIO(const std::string &caminho, int id) {
//     std::ofstream saida;
//     // abre em append binário (mantém arquivo já existente e escreve ao final)
//     saida.open(caminho, std::ios::binary | std::ios::app);
//     if (!saida.is_open()) {
//         std::cerr << "Thread " << id << ": falha ao abrir arquivo " << caminho << "\n";
//         return; // não conseguiu abrir → termina a thread
//     }

//     // buffer preenchido com um byte identificador (id)
//     std::vector<char> buffer(buff, static_cast<char>(id));
//     while (rodando.load()) {
//         saida.write(buffer.data(), static_cast<long int>(buffer.size())); // escreve bloco no arquivo
//         saida.flush();                             // força flush para dispositivo (fazer I/O real)
//         // pequena espera para evitar travar toda a fila de I/O imediatamente
//         std::this_thread::sleep_for(std::chrono::milliseconds(5));
//     }
//     saida.close(); // fecha arquivo ao terminar
// }

// // função que lê metadados/abre arquivo periodicamente (gera leitura leve)
// void leitorIO(const std::string &caminho) {
//     std::vector<char> buffer(buff); // buffer
//     while (rodando.load()) {
//         std::ifstream entrada(caminho, std::ios::binary);
//         if (entrada.is_open()) {
//             while (entrada.read(buffer.data(), static_cast<long int>(buffer.size()))) {
//                 // Apenas lê, não faz nada com os dados
//             }
//             // Para ler o último pedaço, que pode ser menor que buffer.size()
//             entrada.read(buffer.data(), static_cast<long int>(buffer.size()));
//             entrada.close();
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     }
// }

// int main() {
//     unsigned int numeroWriters = std::max(1u, std::thread::hardware_concurrency() / 2);
//     // caminho padrão; pode ser alterado para qualquer diretório com permissão de escrita
//     std::string caminhoArquivo = "/tmp/teste_io.bin";

//     std::cout << "Teste I/O: " << numeroWriters << " writers para '" << caminhoArquivo << "'\n";
//     std::cout << "Pressione ENTER para parar.\n";

//     std::vector<std::thread> threads; // container de threads
//     for (unsigned int i = 0; i < numeroWriters; ++i)
//         threads.emplace_back(escritorIO, caminhoArquivo, (int)i); // cria writers

//     // adiciona um leitor de metadados para gerar operações de leitura
//     threads.emplace_back(leitorIO, caminhoArquivo);

//     std::cin.get();            // espera ENTER
//     rodando.store(false);      // sinaliza parada

//     for (auto &t : threads) if (t.joinable()) t.join(); // aguarda finalização
//     std::cout << "Teste I/O finalizado. arquivo: " << caminhoArquivo << "\n";
//     return 0;
// }

#include <iostream>
#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <sstream>
#include <cerrno>
#include <filesystem>
#include <vector>
#include <regex>
#include <unordered_map>
#include <system_error>
#include <cctype>
#include "monitor.h"

bool coletorIO(StatusProcesso &medicao) {
    int PID = medicao.PID;
    //Checa se o processo existe
    if (!processoExiste(PID)) {
    std::cerr << "Erro: processo " << PID << " inexistente.\n";
    return false;
    }

    //Checa se o programa possuí permissões para acessar os arquivos essenciais
    if (!temPermissao(PID)) {
    std::cerr << "Erro: sem permissão para acessar processo " << PID << ".\n";
    return false;
    }

    std::string pathIO = "/proc/" + std::to_string(PID) + "/io";
    std::ifstream io(pathIO);
    //Checa se conseguimos abrir o arquivo em específico, serve para sabermos se o processo não morreu subitamente
    if(!io.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathIO << "\n";
        std::cerr << "O processo encerrou entre verificações... ou não possui permissões para o arquivo\n";
        return false;
    }

    std::string conteudo;
    while (std::getline(io, conteudo)) {
        if (conteudo.rfind("read_bytes:", 0) == 0)
            sscanf(conteudo.c_str(), "read_bytes: %lu", &medicao.bytesLidos);
        else if (conteudo.rfind("write_bytes:", 0) == 0)
            sscanf(conteudo.c_str(), "write_bytes: %lu", &medicao.bytesEscritos);
        else if (conteudo.rfind("syscr:", 0) == 0)
            sscanf(conteudo.c_str(), "syscr: %lu", &medicao.syscallLeitura);
        else if (conteudo.rfind("syscw:", 0) == 0)
            sscanf(conteudo.c_str(), "syscw: %lu", &medicao.syscallEscrita);
            else if (conteudo.rfind("rchar:", 0) == 0)
            sscanf(conteudo.c_str(), "rchar: %lu", &medicao.rchar);
        else if (conteudo.rfind("wchar:", 0) == 0)
            sscanf(conteudo.c_str(), "wchar: %lu", &medicao.wchar);
    }
    return true;
}


// bool coletorNetwork(StatusProcesso &medicao) {
//     int PID = medicao.PID;

//     if (!processoExiste(PID)) {
//         std::cerr << "Erro: processo " << PID << " inexistente.\n";
//         return false;
//     }

//     if (!temPermissao(PID)) {
//         std::cerr << "Erro: sem permissão para acessar processo " << PID << ".\n";
//         return false;
//     }

//     std::string caminhoFdProcesso = "/proc/" + std::to_string(PID) + "/fd";
//     if (!std::filesystem::exists(caminhoFdProcesso)) {
//         std::cerr << "Erro: fd do processo não existe.\n";
//         return false;
//     }

//     // Contagem de sockets ativos
//     medicao.conexoesAtivas = 0;
//     for (auto &entradaFd : std::filesystem::directory_iterator(caminhoFdProcesso)) {
//         std::string linkDescricaoFd;
//         try {
//             linkDescricaoFd = std::filesystem::read_symlink(entradaFd.path());
//         } catch (...) {
//             continue;
//         }
//         if (linkDescricaoFd.find("socket:[") != std::string::npos) {
//             medicao.conexoesAtivas++;
//         }
//     }

//     // Leitura de bytes lidos/escritos (rchar/wchar)
//     std::string pathIo = "/proc/" + std::to_string(PID) + "/io";
//     std::ifstream ioStream(pathIo);
//     if (!ioStream.is_open()) {
//         std::cerr << "Erro: não foi possível abrir " << pathIo << "\n";
//         return false;
//     }

//     std::string linha;
//     while (std::getline(ioStream, linha)) {
//         if (linha.rfind("rchar:", 0) == 0)
//             sscanf(linha.c_str(), "rchar: %lu", &medicao.bytesRx);
//         else if (linha.rfind("wchar:", 0) == 0)
//             sscanf(linha.c_str(), "wchar: %lu", &medicao.bytesTx);
//     }

//     // Estimativa de pacotes
//     medicao.pacotesRecebidos = medicao.bytesRx / 1500;
//     medicao.pacotesEnviados = medicao.bytesTx / 1500;

//     return true;
// }


bool coletorNetwork(StatusProcesso &status) { // entrada: recebe estrutura onde vai gravar métricas
    const int pid = status.PID; // PID do processo alvo
    const std::string caminhoDiretorioFd = "/proc/" + std::to_string(pid) + "/fd"; // path para os file descriptors do processo

    status.bytesRxfila = 0; // zera contador de bytes em fila de recepção
    status.bytesTxfila = 0; // zera contador de bytes em fila de transmissão
    status.conexoesAtivas = 0; // zera contador de sockets encontrados

    std::vector<unsigned long> listaInodesSockets; // vetor para armazenar inodes de sockets do processo
    if (std::filesystem::exists(caminhoDiretorioFd)) { // verifica existência do diretório /proc/<pid>/fd
        for (auto &entradaFd : std::filesystem::directory_iterator(caminhoDiretorioFd, std::filesystem::directory_options::skip_permission_denied)) { // itera FDs ignorando erros de permissão
            std::error_code codigoErro; // recebe erro de read_symlink sem lançar exceção
            auto linkSimbolico = std::filesystem::read_symlink(entradaFd.path(), codigoErro); // lê o symlink do FD (ex.: "socket:[12345]" ou "pipe:[5678]")
            if (codigoErro) continue; // se falhou ao ler o link, pula este FD
            std::string alvoLink = linkSimbolico.string(); // converte o path do symlink para string

            if (alvoLink.rfind("socket:[", 0) == 0) { // filtra apenas entradas que começam com "socket:["
                auto posAbertura = alvoLink.find('['); // posição do '['
                auto posFechamento = alvoLink.find(']'); // posição do ']'
                if (posAbertura != std::string::npos && posFechamento != std::string::npos && posFechamento > posAbertura+1) { // garante formato válido com número entre colchetes
                    try {
                        unsigned long numeroInode = std::stoul(alvoLink.substr(posAbertura+1, posFechamento-posAbertura-1), nullptr, 10); // extrai número do inode do texto
                        listaInodesSockets.push_back(numeroInode); // adiciona inode à lista
                    } catch (...) { continue; } // se conversão falhar, ignora este FD
                }
            }
        }
    }

    status.conexoesAtivas = static_cast<unsigned int>(listaInodesSockets.size()); // grava quantos sockets foram detectados no processo
    if (listaInodesSockets.empty()) return true; // se não há sockets, retorna rapidamente

    std::unordered_map<unsigned long, std::pair<unsigned long, unsigned long>> tabelaInodeParaFilas; // mapa: inode -> (rxQueue, txQueue)
    auto lerTabelaRede = [&](const std::string &caminhoTabela) { // lambda que faz parse de um /proc/net/*
        std::ifstream arquivoRede(caminhoTabela); // abre arquivo de tabela de sockets (tcp/udp)
        if (!arquivoRede.is_open()) return; // se não abrir, sai da lambda
        std::string linhaTabela;
        std::getline(arquivoRede, linhaTabela); // descarta cabeçalho (primeira linha)

        while (std::getline(arquivoRede, linhaTabela)) { // itera linhas restantes, cada uma descreve um socket
            if (linhaTabela.empty()) continue; // pula linhas vazias

            std::istringstream streamTokens(linhaTabela); // stream para tokenizar a linha por whitespace
            std::vector<std::string> listaTokens; // vetor temporário com tokens da linha
            std::string token;
            while (streamTokens >> token) listaTokens.push_back(token); // enche listaTokens com campos da linha
            if (listaTokens.empty()) continue; // se não houver tokens, pula

            std::string campoTxRxHex; // vai receber token no formato hex:hex (tx:rx)
            for (const auto &t : listaTokens) { // procura token que contenha ':' e seja hex:hex
                auto pos = t.find(':'); // posição do ':'
                if (pos == std::string::npos) continue; // não é o token TX:RX

                std::string parteEsquerda = t.substr(0, pos); // parte antes dos dois pontos (tx em hex)
                std::string parteDireita = t.substr(pos+1); // parte depois (rx em hex)
                if (parteEsquerda.empty() || parteDireita.empty()) continue; // precisa ter ambos os lados

                bool esquerdaHex = true, direitaHex = true; // flags de validação hex
                for (char c : parteEsquerda) if (!std::isxdigit((unsigned char)c)) { esquerdaHex = false; break; } // valida caracteres hex à esquerda
                for (char c : parteDireita) if (!std::isxdigit((unsigned char)c)) { direitaHex = false; break; } // valida caracteres hex à direita

                if (esquerdaHex && direitaHex) { campoTxRxHex = t; break; } // encontrou token válido TX:RX
            }
            if (campoTxRxHex.empty()) continue; // se não achou TX:RX, pula linha

            unsigned long valorTxFila = 0, valorRxFila = 0; // placeholders para valores convertidos
            try {
                auto separador = campoTxRxHex.find(':'); // separa hex tx e hex rx
                std::string hexTx = campoTxRxHex.substr(0, separador); // hex TX
                std::string hexRx = campoTxRxHex.substr(separador + 1); // hex RX
                valorTxFila = std::stoul(hexTx, nullptr, 16); // converte TX hex -> inteiro decimal
                valorRxFila = std::stoul(hexRx, nullptr, 16); // converte RX hex -> inteiro decimal
            } catch (...) { continue; } // em qualquer erro de conversão, pula linha

            unsigned long inodeLinha = 0; // vai receber o inode extraído da linha
            for (auto it = listaTokens.rbegin(); it != listaTokens.rend(); ++it) { // percorre tokens de trás pra frente procurando o inode numérico
                const std::string &tokenAtual = *it;
                if (tokenAtual.empty()) continue; // pula token vazio

                bool somenteDigitos = true;
                for (char c : tokenAtual) if (!std::isdigit((unsigned char)c)) { somenteDigitos = false; break; } // verifica se token é só dígitos

                if (somenteDigitos) {
                    try { inodeLinha = std::stoul(tokenAtual, nullptr, 10); } catch (...) { inodeLinha = 0; } // tenta converter para inteiro
                    if (inodeLinha != 0) break; // se converteu e não é zero, usa como inode
                }
            }

            if (inodeLinha == 0) continue; // sem inode válido, pula linha
            tabelaInodeParaFilas[inodeLinha] = {valorRxFila, valorTxFila}; // registra rx/tx para o inode
        }
    };

    lerTabelaRede("/proc/net/tcp"); // popula tabela com entradas TCP IPv4
    lerTabelaRede("/proc/net/tcp6"); // popula tabela com entradas TCP IPv6
    lerTabelaRede("/proc/net/udp"); // popula tabela com entradas UDP IPv4
    lerTabelaRede("/proc/net/udp6"); // popula tabela com entradas UDP IPv6

    for (unsigned long inodeAtual : listaInodesSockets) { // para cada inode que pertence ao processo
        auto it = tabelaInodeParaFilas.find(inodeAtual); // busca estatísticas desse inode na tabela de rede
        if (it != tabelaInodeParaFilas.end()) { // se encontrado (é socket de rede)
            status.bytesRxfila += it->second.first; // acumula bytes em fila de recepção
            status.bytesTxfila += it->second.second; // acumula bytes em fila de transmissão
        }
    }

    return true; // coleta concluída com sucesso
}


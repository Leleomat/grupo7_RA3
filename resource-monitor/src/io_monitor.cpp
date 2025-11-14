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


bool coletorNetwork(StatusProcesso &medicao) {
    int PID = medicao.PID;

    if (!processoExiste(PID)) {
        std::cerr << "Erro: processo " << PID << " inexistente.\n";
        return false;
    }

    if (!temPermissao(PID)) {
        std::cerr << "Erro: sem permissão para acessar processo " << PID << ".\n";
        return false;
    }

    std::string caminhoFdProcesso = "/proc/" + std::to_string(PID) + "/fd";
    if (!std::filesystem::exists(caminhoFdProcesso)) {
        std::cerr << "Erro: fd do processo não existe.\n";
        return false;
    }

    // Contagem de sockets ativos
    medicao.conexoesAtivas = 0;
    for (auto &entradaFd : std::filesystem::directory_iterator(caminhoFdProcesso)) {
        std::string linkDescricaoFd;
        try {
            linkDescricaoFd = std::filesystem::read_symlink(entradaFd.path());
        } catch (...) {
            continue;
        }
        if (linkDescricaoFd.find("socket:[") != std::string::npos) {
            medicao.conexoesAtivas++;
        }
    }

    // Leitura de bytes lidos/escritos (rchar/wchar)
    std::string pathIo = "/proc/" + std::to_string(PID) + "/io";
    std::ifstream ioStream(pathIo);
    if (!ioStream.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << pathIo << "\n";
        return false;
    }

    std::string linha;
    while (std::getline(ioStream, linha)) {
        if (linha.rfind("rchar:", 0) == 0)
            sscanf(linha.c_str(), "rchar: %lu", &medicao.bytesRx);
        else if (linha.rfind("wchar:", 0) == 0)
            sscanf(linha.c_str(), "wchar: %lu", &medicao.bytesTx);
    }

    // Estimativa de pacotes
    medicao.pacotesRecebidos = medicao.bytesRx / 1500;
    medicao.pacotesEnviados = medicao.bytesTx / 1500;

    return true;
}

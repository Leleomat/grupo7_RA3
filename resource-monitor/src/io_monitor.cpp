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
        std::cerr << "O processo encerrou entre verificações...\n";
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

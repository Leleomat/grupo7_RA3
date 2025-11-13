#include <iostream>
#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include "monitor.h"

bool coletorMemoria(StatusProcesso &medicao) {
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

    //Coleta de vmsize, vmswap e vmrss
    std::string pathStatus = "/proc/" + std::to_string(PID) + "/status";
    std::ifstream status(pathStatus);
    //Checa se conseguimos abrir o arquivo em específico, serve para sabermos se o processo não morreu subitamente
    if(!status.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStatus << "\n";
        std::cerr << "O processo encerrou entre verificações...\n";
        return false;
    }
    std::string conteudo;
    while(std::getline(status,conteudo)){
        if (conteudo.rfind("VmSize:", 0) == 0)
            sscanf(conteudo.c_str(), "VmSize: %lu", &medicao.vmSize);
        else if (conteudo.rfind("VmSwap:", 0) == 0)
            sscanf(conteudo.c_str(), "VmSwap: %lu", &medicao.vmSwap);
        else if (conteudo.rfind("VmRSS:", 0) == 0)
            sscanf(conteudo.c_str(), "VmRSS: %lu", &medicao.vmRss);
    }
    //Coleta do page faults
    std::string pathStat = "/proc/" + std::to_string(PID) + "/stat";
    std::ifstream stat(pathStat);
    //Checa se conseguimos abrir o arquivo em específico, serve para sabermos se o processo não morreu subitamente
    if(!stat.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStat << "\n";
        std::cerr << "O processo encerrou entre verificações...\n";
        return false;
    }
    unsigned long mjrfault=0, minfault=0;
    
    std::getline(stat,conteudo);
    auto aposParenteses = conteudo.rfind(')');
    std::string depois = conteudo.substr(aposParenteses+1);
    std::istringstream fluxoLeitura(depois);
    std::string iterador;

    for (int i = 1; i <= 7; ++i)
        fluxoLeitura >> iterador;
    fluxoLeitura >> minfault;
    fluxoLeitura >> iterador >> mjrfault;

    medicao.mjrfault = mjrfault;
    medicao.minfault = minfault;

    return true;

}

#include <iostream>
#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include "monitor.h"

bool temPermissao(int PID) {
    std::string path = "/proc/" + std::to_string(PID) + "/status";
    return access(path.c_str(), R_OK) == 0;
}

bool coletorCPU(StatusProcesso &medicao){
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

    //Coleta do utime e stime para cálculo de CPU 
    std::string pathStat = "/proc/" + std::to_string(PID) + "/stat";
    std::ifstream stat(pathStat);
    //Checa se conseguimos abrir o arquivo em específico, serve para sabermos se o processo não morreu subitamente
    if(!stat.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStat << "\n";
        std::cerr << "O processo encerrou entre verificações... ou não possui permissões para o arquivo\n\n";
        return false;
    }


    double userTime=0, systemTime=0;
    std::string conteudo;

    //Acha o fim do campo do nome do processo
    std::getline(stat,conteudo);
    auto aposParenteses = conteudo.rfind(')');
    std::string depois = conteudo.substr(aposParenteses+1);
    std::istringstream fluxoLeitura(depois);
    std::string iterador;

    for (int i = 1; i <= 11; ++i)
        fluxoLeitura >> iterador;

    fluxoLeitura >> userTime >> systemTime;

    long tickSegundo = sysconf(_SC_CLK_TCK);

    medicao.utime = static_cast<double>(userTime)/static_cast<double>(tickSegundo);
    medicao.stime = static_cast<double>(systemTime)/static_cast<double>(tickSegundo);

    //Coleta de threads e context switches
    std::string pathStatus = "/proc/" + std::to_string(PID) + "/status";
    std::ifstream status(pathStatus);
    if(!status.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStatus << "\n";
        std::cerr << "O processo encerrou entre verificações... ou não possui permissões para o arquivo\n\n";
        return false;
    }

    while(std::getline(status,conteudo)){
        if (conteudo.rfind("voluntary_ctxt_switches:", 0) == 0)
            sscanf(conteudo.c_str(), "voluntary_ctxt_switches: %u", &medicao.contextSwitchfree);
        else if (conteudo.rfind("nonvoluntary_ctxt_switches:", 0) == 0)
            sscanf(conteudo.c_str(), "nonvoluntary_ctxt_switches: %u", &medicao.contextSwitchforced);
        else if (conteudo.rfind("Threads:", 0) == 0)
            sscanf(conteudo.c_str(), "Threads: %u", &medicao.threads);
    }
    return true;
}

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
    int pid = medicao.PID; // pega o PID do processo monitorado

    //Verifica se o processo existe
    if (!processoExiste(pid)) {
        std::cerr << "Erro: processo " << pid << " inexistente.\n";
        return false; // retorna false se processo não existe
    }

    //Verifica se temos permissão para acessar arquivos do processo
    if (!temPermissao(pid)) {
        std::cerr << "Erro: sem permissão para acessar processo " << pid << ".\n";
        return false; // retorna false se não há permissão
    }

    //Coleta memória virtual (VmSize), swap (VmSwap) e residente (VmRSS)
    std::string pathStatus = "/proc/" + std::to_string(pid) + "/status"; // caminho do arquivo status
    std::ifstream arquivoStatus(pathStatus); // abre arquivo status
    if (!arquivoStatus.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << pathStatus << "\n";
        std::cerr << "O processo encerrou ou não possui permissões para o arquivo\n\n";
        return false; // retorna false se não abriu
    }

    std::string linha;
    while (std::getline(arquivoStatus, linha)) { // lê linha por linha
        if (linha.rfind("VmSize:", 0) == 0) // começa com VmSize
            sscanf(linha.c_str(), "VmSize: %lu", &medicao.vmSize); // atualiza vmSize
        else if (linha.rfind("VmSwap:", 0) == 0) // começa com VmSwap
            sscanf(linha.c_str(), "VmSwap: %lu", &medicao.vmSwap); // atualiza vmSwap
        else if (linha.rfind("VmRSS:", 0) == 0) // começa com VmRSS
            sscanf(linha.c_str(), "VmRSS: %lu", &medicao.vmRss); // atualiza vmRss
    }

    //Coleta de page faults
    std::string pathStat = "/proc/" + std::to_string(pid) + "/stat"; // caminho do arquivo stat
    std::ifstream arquivoStat(pathStat); // abre arquivo stat
    if (!arquivoStat.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << pathStat << "\n";
        std::cerr << "O processo encerrou ou não possui permissões para o arquivo\n\n";
        return false; // retorna false se não abriu
    }

    unsigned long majorFault = 0, minorFault = 0; // variáveis para page faults
    std::getline(arquivoStat, linha); // lê primeira linha inteira
    auto aposParenteses = linha.rfind(')'); // procura fechamento de parêntese (nome do processo)
    std::string depois = linha.substr(aposParenteses + 1); // pega substring após parêntese
    std::istringstream fluxo(depois); // cria fluxo de leitura
    std::string iterador;

    // pular os primeiros 7 campos após o parêntese
    for (int i = 1; i <= 7; ++i)
        fluxo >> iterador;

    fluxo >> minorFault; // captura minor faults
    fluxo >> iterador;   // pular campo desnecessário
    fluxo >> majorFault; // captura major faults

    medicao.minfault = minorFault; // atualiza struct
    medicao.mjrfault = majorFault; // atualiza struct

    return true; // retorna true se tudo ocorreu sem erro
}

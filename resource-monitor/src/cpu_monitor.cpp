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
#include <sys/wait.h>
#include "monitor.h"
#include <numeric>

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


void cargaExecutar() {
    std::cout << "Processo filho iniciado, PID: " << getpid() << "\n";

    double resultadoAcumulado = 0.0;
    auto tempoInicio = std::chrono::steady_clock::now();
    int duracaoSegundos = 5; // tempo que o código vai rodar

    while (true) {
        // cálculos para usar CPU moderadamente
        for (int i = 1; i <= 10000; ++i)
            resultadoAcumulado += std::sqrt(i) * std::sin(i);

        // verifica se atingiu o tempo
        auto tempoAtual = std::chrono::steady_clock::now();
        double segundosDecorridos = std::chrono::duration<double>(tempoAtual - tempoInicio).count();
        if (segundosDecorridos >= duracaoSegundos)
            break;

        // pequena pausa para não saturar CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "Processo filho terminou, resultado: " << resultadoAcumulado << "\n";
}

std::pair<unsigned long, unsigned long> lerCPU(int pid) {
    // Abre o arquivo /proc/[pid]/stat
    std::ifstream arquivo("/proc/" + std::to_string(pid) + "/stat");
    std::string linha;
    std::getline(arquivo, linha);

    // Separa os campos da linha
    std::istringstream iss(linha);
    std::vector<std::string> campos;
    std::string campo;
    while (iss >> campo)
        campos.push_back(campo);

    // utime = 14º campo, stime = 15º campo (índice 13 e 14)
    unsigned long utime = std::stoul(campos[13]);
    unsigned long stime = std::stoul(campos[14]);

    return {utime, stime};
}

double coletarAmostra(StatusProcesso &status) {
    auto inicio = std::chrono::steady_clock::now();

    coletorCPU(status);
    coletorMemoria(status);
    coletorIO(status);
    coletorNetwork(status);

    auto fim = std::chrono::steady_clock::now();
    double latenciaMs = std::chrono::duration<double, std::milli>(fim - inicio).count();
    return latenciaMs; // retorna latência da coleta
}

// Função para calcular CPU% em um intervalo
double calcularCpuPercent(unsigned long utimeInicio, unsigned long stimeInicio,unsigned long utimeFim, unsigned long stimeFim,double intervaloSegundos) {
    long ticks = sysconf(_SC_CLK_TCK);
    double cpuSegundos = static_cast<double>(utimeFim - utimeInicio + stimeFim - stimeInicio) / static_cast<double>(ticks);
    return (cpuSegundos / intervaloSegundos) * 100.0;
}

void overheadMonitoramento() {
    pid_t PID;
    int duracaoCarga = 5; // segundos de execução da carga

    // === Baseline (sem monitoramento) ===
    PID = fork();
    if (PID == 0) {
        cargaExecutar();
        _exit(0);
    } else {
        auto inicioBase = std::chrono::steady_clock::now();
        auto [utimeIniBase, stimeIniBase] = lerCPU(PID);

        std::this_thread::sleep_for(std::chrono::duration<double>(4.5));
        auto [utimeFimBase, stimeFimBase] = lerCPU(PID);
        waitpid(PID, nullptr, 0);
        auto fimBase = std::chrono::steady_clock::now();

        double tempoExecBase = std::chrono::duration<double>(fimBase - inicioBase).count();
        double cpuBaseline = calcularCpuPercent(utimeIniBase, stimeIniBase, utimeFimBase, stimeFimBase, tempoExecBase);

        std::cout << "=== Baseline (sem monitoramento) ===\n";
        std::cout << "CPU% baseline: " << cpuBaseline << "\n";
        std::cout << "Tempo execução baseline: " << tempoExecBase << " s\n\n";

        // === Monitorado (com sampling) ===
        std::vector<int> intervalosMs = {250, 500, 1000};
        for (int intervalo : intervalosMs) {
            PID = fork();
            if (PID == 0) {
                cargaExecutar();
                _exit(0);
            } else {
                std::vector<double> cpuAmostras;
                std::vector<double> latenciasSampling;
                StatusProcesso status;
                status.PID = PID;

                auto tempoInicio = std::chrono::steady_clock::now();
                auto [utimeIni, stimeIni] = lerCPU(PID);

                while (true) {
                    auto iterInicio = std::chrono::steady_clock::now();
                    double latenciaMs = coletarAmostra(status);
                    latenciasSampling.push_back(latenciaMs);

                    std::this_thread::sleep_for(std::chrono::milliseconds(intervalo));

                    auto iterFim = std::chrono::steady_clock::now();
                    double segundosDecorridos = std::chrono::duration<double>(iterFim - tempoInicio).count();
                    if (segundosDecorridos >= duracaoCarga)
                        break;

                    auto [utimeAtual, stimeAtual] = lerCPU(PID);
                    auto duracaoIter = std::chrono::duration<double>(iterFim - iterInicio).count();

                    double cpuPercent = calcularCpuPercent(utimeIni, stimeIni, utimeAtual, stimeAtual, duracaoIter);
                    cpuAmostras.push_back(cpuPercent);

                    utimeIni = utimeAtual;
                    stimeIni = stimeAtual;
                }

                waitpid(PID, nullptr, 0);
                auto tempoFim = std::chrono::steady_clock::now();
                double tempoExecMonitorado = std::chrono::duration<double>(tempoFim - tempoInicio).count();

                double cpuMedio = std::accumulate(cpuAmostras.begin(), cpuAmostras.end(), 0.0) / static_cast<double>(cpuAmostras.size());
                double overhead = cpuMedio - cpuBaseline;
                double latenciaMedia = std::accumulate(latenciasSampling.begin(), latenciasSampling.end(), 0.0) / static_cast<double>(latenciasSampling.size());

                std::cout << "=== Monitorado (intervalo " << intervalo << " ms) ===\n";
                std::cout << "CPU% médio monitorado: " << cpuMedio << "\n";
                std::cout << "Overhead CPU%: " << overhead << "\n";
                std::cout << "Tempo execução baseline: " << tempoExecBase << " s\n";
                std::cout << "Tempo execução monitorado: " << tempoExecMonitorado << " s\n";
                std::cout << "Latência média sampling: " << latenciaMedia << " ms\n\n";
            }
        }
    }
}

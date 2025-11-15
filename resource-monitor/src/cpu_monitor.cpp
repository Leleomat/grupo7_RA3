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
    // Cria o path para o arquivo /proc/[PID]/status do processo
    std::string path = "/proc/" + std::to_string(PID) + "/status";
    // Verifica se o arquivo pode ser lido (R_OK = read permission)
    // Retorna true se acessível, false se não
    return access(path.c_str(), R_OK) == 0;
}

bool coletorCPU(StatusProcesso &medicao){
    int PID = medicao.PID; // guarda PID do processo monitorado

    // Checa se o processo existe
    if (!processoExiste(PID)) {
        std::cerr << "Erro: processo " << PID << " inexistente.\n";
        return false; // sai se processo não existe
    }

    // Checa se temos permissão para acessar arquivos do processo
    if (!temPermissao(PID)) {
        std::cerr << "Erro: sem permissão para acessar processo " << PID << ".\n";
        return false; // sai se sem permissão
    }

    // Abre o arquivo stat para pegar utime e stime (tempos de CPU)
    std::string pathStat = "/proc/" + std::to_string(PID) + "/stat";
    std::ifstream stat(pathStat);
    if(!stat.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStat << "\n";
        std::cerr << "O processo encerrou ou sem permissões\n\n";
        return false; // sai se não conseguiu abrir
    }

    double userTime=0, systemTime=0; // variáveis para armazenar utime e stime
    std::string conteudo;

    // Lê toda a linha do arquivo stat
    std::getline(stat,conteudo);
    // Procura a posição do último parêntese, que fecha o nome do processo
    auto aposParenteses = conteudo.rfind(')');
    // Pega substring depois do nome do processo
    std::string depois = conteudo.substr(aposParenteses+1);
    std::istringstream fluxoLeitura(depois); // cria fluxo para ler os campos restantes
    std::string iterador;

    // Pula os primeiros 11 campos restantes (não nos interessam)
    for (int i = 1; i <= 11; ++i)
        fluxoLeitura >> iterador;

    // Lê utime e stime (tempo de CPU em ticks)
    fluxoLeitura >> userTime >> systemTime;

    // Obtém número de ticks por segundo do sistema
    long tickSegundo = sysconf(_SC_CLK_TCK);

    // Converte de ticks para segundos e salva no struct
    medicao.utime = static_cast<double>(userTime)/static_cast<double>(tickSegundo);
    medicao.stime = static_cast<double>(systemTime)/static_cast<double>(tickSegundo);

    // Abre o arquivo status para pegar threads e context switches
    std::string pathStatus = "/proc/" + std::to_string(PID) + "/status";
    std::ifstream status(pathStatus);
    if(!status.is_open()){
        std::cerr << "Erro: não foi possível abrir " << pathStatus << "\n";
        std::cerr << "O processo encerrou ou sem permissões\n\n";
        return false; // sai se não conseguiu abrir
    }

    // Lê linha por linha e extrai informações de interesse
    while(std::getline(status,conteudo)){
        if (conteudo.rfind("voluntary_ctxt_switches:", 0) == 0)
            sscanf(conteudo.c_str(), "voluntary_ctxt_switches: %u", &medicao.contextSwitchfree);
        else if (conteudo.rfind("nonvoluntary_ctxt_switches:", 0) == 0)
            sscanf(conteudo.c_str(), "nonvoluntary_ctxt_switches: %u", &medicao.contextSwitchforced);
        else if (conteudo.rfind("Threads:", 0) == 0)
            sscanf(conteudo.c_str(), "Threads: %u", &medicao.threads);
    }
    return true; // coleta bem-sucedida
}

void cargaExecutar() {
    // Imprime PID do processo filho
    std::cout << "Processo filho iniciado, PID: " << getpid() << "\n";

    double resultadoAcumulado = 0.0; // resultado da carga para usar CPU
    auto tempoInicio = std::chrono::steady_clock::now(); // marca tempo de início
    int duracaoSegundos = 5; // define tempo de execução da carga

    while (true) {
        // Loop pesado para consumir CPU
        for (int i = 1; i <= 10000; ++i)
            resultadoAcumulado += std::sqrt(i) * std::sin(i);

        // Verifica tempo decorrido
        auto tempoAtual = std::chrono::steady_clock::now();
        double segundosDecorridos = std::chrono::duration<double>(tempoAtual - tempoInicio).count();
        if (segundosDecorridos >= duracaoSegundos)
            break; // sai quando tempo atingir

        // Pausa curta para não saturar totalmente a CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Imprime resultado final da carga
    std::cout << "Processo filho terminou, resultado: " << resultadoAcumulado << "\n";
}

std::pair<unsigned long, unsigned long> lerCPU(int pid) {
    // Lê arquivo /proc/[pid]/stat
    std::ifstream arquivo("/proc/" + std::to_string(pid) + "/stat");
    std::string linha;
    std::getline(arquivo, linha); // pega primeira linha

    // Separa a linha em campos usando espaço como delimitador
    std::istringstream iss(linha);
    std::vector<std::string> campos;
    std::string campo;
    while (iss >> campo)
        campos.push_back(campo);

    // utime = 14º campo, stime = 15º campo (índices 13 e 14)
    unsigned long utime = std::stoul(campos[13]);
    unsigned long stime = std::stoul(campos[14]);

    return {utime, stime}; // retorna pair com utime e stime
}

double coletarAmostra(StatusProcesso &status) {
    // Marca início da coleta
    auto inicio = std::chrono::steady_clock::now();

    // Coleta métricas de CPU, memória, I/O e rede
    coletorCPU(status);
    coletorMemoria(status);
    coletorIO(status);
    coletorNetwork(status);

    // Calcula latência da coleta em milissegundos
    auto fim = std::chrono::steady_clock::now();
    double latenciaMs = std::chrono::duration<double, std::milli>(fim - inicio).count();
    return latenciaMs; 
}

double calcularCpuPercent(unsigned long utimeInicio, unsigned long stimeInicio,
                          unsigned long utimeFim, unsigned long stimeFim,
                          double intervaloSegundos) {
    // Obtém ticks por segundo
    long ticks = sysconf(_SC_CLK_TCK);
    // Calcula tempo total de CPU usado entre dois instantes
    double cpuSegundos = static_cast<double>(utimeFim - utimeInicio + stimeFim - stimeInicio) / static_cast<double>(ticks);
    // Converte para porcentagem em relação ao intervalo
    return (cpuSegundos / intervaloSegundos) * 100.0;
}

void overheadMonitoramento() {
    pid_t PID; // PID do processo filho
    int duracaoCarga = 5; // tempo de execução da carga em segundos

    // === Baseline sem monitoramento ===
    PID = fork(); // cria processo filho
    if (PID == 0) {
        cargaExecutar(); // executa carga no filho
        _exit(0); // finaliza filho
    } else {
        // Pai: marca tempo inicial
        auto inicioBase = std::chrono::steady_clock::now();
        // Lê utime e stime inicial do processo filho
        auto [utimeIniBase, stimeIniBase] = lerCPU(PID);

        // Espera quase toda a duração da carga
        std::this_thread::sleep_for(std::chrono::duration<double>(4.5));
        // Lê utime e stime final
        auto [utimeFimBase, stimeFimBase] = lerCPU(PID);
        waitpid(PID, nullptr, 0); // espera filho terminar
        auto fimBase = std::chrono::steady_clock::now();

        // Calcula tempo de execução real e uso de CPU baseline
        double tempoExecBase = std::chrono::duration<double>(fimBase - inicioBase).count();
        double cpuBaseline = calcularCpuPercent(utimeIniBase, stimeIniBase, utimeFimBase, stimeFimBase, tempoExecBase);

        // Imprime resultados baseline
        std::cout << "=== Baseline (sem monitoramento) ===\n";
        std::cout << "CPU% baseline: " << cpuBaseline << "\n";
        std::cout << "Tempo execução baseline: " << tempoExecBase << " s\n\n";

        // === Monitorado com sampling ===
        std::vector<int> intervalosMs = {250, 500, 1000}; // intervalos de amostragem em ms
        for (int intervalo : intervalosMs) {
            PID = fork(); // cria novo processo filho
            if (PID == 0) {
                cargaExecutar(); // filho executa carga
                _exit(0);
            } else {
                std::vector<double> cpuAmostras; // guarda amostras de CPU%
                std::vector<double> latenciasSampling; // guarda latência de coleta
                StatusProcesso status;
                status.PID = PID;

                auto tempoInicio = std::chrono::steady_clock::now();
                auto [utimeIni, stimeIni] = lerCPU(PID); // utime/stime inicial

                while (true) {
                    auto iterInicio = std::chrono::steady_clock::now();
                    // coleta amostra completa (CPU, memória, I/O, rede)
                    double latenciaMs = coletarAmostra(status);
                    latenciasSampling.push_back(latenciaMs);

                    // espera intervalo definido
                    std::this_thread::sleep_for(std::chrono::milliseconds(intervalo));

                    auto iterFim = std::chrono::steady_clock::now();
                    double segundosDecorridos = std::chrono::duration<double>(iterFim - tempoInicio).count();
                    if (segundosDecorridos >= duracaoCarga)
                        break;

                    // lê CPU atual do filho
                    auto [utimeAtual, stimeAtual] = lerCPU(PID);
                    auto duracaoIter = std::chrono::duration<double>(iterFim - iterInicio).count();

                    // calcula uso de CPU no intervalo
                    double cpuPercent = calcularCpuPercent(utimeIni, stimeIni, utimeAtual, stimeAtual, duracaoIter);
                    cpuAmostras.push_back(cpuPercent);

                    // atualiza utime/stime inicial para próxima iteração
                    utimeIni = utimeAtual;
                    stimeIni = stimeAtual;
                }

                waitpid(PID, nullptr, 0); // espera filho terminar
                auto tempoFim = std::chrono::steady_clock::now();
                double tempoExecMonitorado = std::chrono::duration<double>(tempoFim - tempoInicio).count();

                // calcula média CPU e overhead
                double cpuMedio = std::accumulate(cpuAmostras.begin(), cpuAmostras.end(), 0.0) / static_cast<double>(cpuAmostras.size());
                double overhead = cpuMedio - cpuBaseline; // diferença em relação ao baseline
                double latenciaMedia = std::accumulate(latenciasSampling.begin(), latenciasSampling.end(), 0.0) / static_cast<double>(latenciasSampling.size());

                // imprime métricas do monitoramento
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

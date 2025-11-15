#include "cgroup.h"               // Cabeçalho da classe CGroupManager (declarações de métodos e structs).
#include <fstream>                // std::ifstream / std::ofstream para leitura/escrita em arquivos.
#include <filesystem>             // std::filesystem para manipulação de paths e diretórios.
#include <iostream>               // std::cout / std::cerr para saída no terminal.
#include <sstream>                // std::istringstream, std::ostringstream — parsing de strings.
#include <vector>                 // std::vector para vetores dinâmicos.
#include <unistd.h>               // getpid(), fork(), usleep(), etc. (POSIX).
#include <thread>                 // std::thread, std::this_thread::sleep_for — temporizações.
#include <atomic>                 // std::atomic (incluído, mas não usado explicitamente aqui).
#include <chrono>                 // std::chrono para medir intervalos de tempo.
#include <cstring>                // memset, etc. (C string ops).
#include <sys/types.h>            // tipos POSIX (pid_t, etc.).
#include <sys/wait.h>             // waitpid, macros WIFEXITED, etc.
#include <signal.h>               // kill, sinais (SIGKILL, etc.).
#include <cmath>

namespace fs = std::filesystem; // Cria um alias `fs` para `std::filesystem`

// Construtor: inicializa o atributo basePath com o valor em `path`.
// Uso: CGroupManager mgr("/sys/fs/cgroup/"); — basePath aponta para onde os cgroups estão montados.
CGroupManager::CGroupManager(const std::string& path) : basePath(path) {}

// Cria um diretório para o cgroup (basePath + name)
bool CGroupManager::createCGroup(const std::string& name) {
    try {
        fs::create_directory(basePath + name); // Tenta criar o diretório; se já existir, pode lançar ou retornar false
        return true; // Caso consiga criar o diretório retorna true
    }
    catch (const std::exception& e) { // Captura exceções do filesystem
        std::cerr << "Erro ao criar cgroup: " << e.what() << std::endl; // Informa o erro detalhado
        return false; // Retorna false
    }
}

// Move um processo para um cgroup escrevendo o pid (process id) em cgroup.procs. Recebe como parâmetro o nome do cgroup e o pid
bool CGroupManager::moveProcessToCGroup(const std::string& name, int pid) {
    std::ofstream tasks(basePath + name + "/cgroup.procs"); // Abre o arquivo cgroup.procs para escrita (ofstream)
    if (!tasks.is_open()) return false; // Se não abriu, retorna false (permissão ou path inválido)
    tasks << pid; // Escreve o pid no arquivo 
    return true; // Retorna sucesso
}

// Define limite de CPU (cgroup v2) escrevendo em cpu.max. Recebe como parâmetro o nome do cgroup e a quantidade de cores que vai ser configurado
// Cores: número de "núcleos" (ex: 0.5, 1.0). cores < 0 => sem limite 
bool CGroupManager::setCpuLimit(const std::string& name, double cores) {
    // Monta o caminho completo para o arquivo cpu.max dentro do cgroup. Em cgroup v2, esse arquivo controla o limite máximo de CPU.
    std::string path = basePath + name + "/cpu.max";

    // Verifica se o arquivo realmente existe.
    if (!fs::exists(path)) {
        std::cerr << "Erro: " << path << " não existe.\n";
        return false; // Não há como aplicar limite sem esse arquivo, retorna false.
    }

    // Abre o arquivo cpu.max para escrita. Escrever nesse arquivo define o limite de CPU do cgroup.
    std::ofstream cpuMax(path);

    // Se falhou ao abrir mostra erro ao usuário
    if (!cpuMax.is_open()) {
        std::cerr << "Erro ao abrir " << path << "\n";
        return false;
    }

    // Define o período padrão para cálculo de limite (100000 microssegundos = 100ms)
    // Esse é o período tradicional usado pelo kernel e pelo Docker.
    const uint64_t period = 100000; // 100ms

    // Verifica se o valor passado é válido.
    // isfinite evita valores como NaN, +inf, -inf.
    if (!std::isfinite(cores)) {
        std::cerr << "Valor inválido para cores.\n";
        return false;
    }

    // Caso o usuário passe 'cores < 0', interpreta como "sem limite". Segue o padrão do kernel Linux ("max" significa ilimitado).
    if (cores < 0) {
        cpuMax << "max " << period; // Formato: "max 100000"
    }
    else {
        // Calcula o "quota" (número de núcleos * período) permitido. Exemplo: cores=0.5 → quota=0.5 * 100000 = 50000 µs (50%)
        uint64_t quota = static_cast<uint64_t>(cores * period);

        // Evita quota = 0 
        if (quota == 0)
            quota = 1;

        // Escreve o limite no formato obrigatório do cgroup v2: "<quota> <period>"
        cpuMax << quota << " " << period;
    }

    // .flush() garante que os dados sejam escritos no arquivo.
    cpuMax.flush();

    // Verifica se ocorreu algum erro durante a escrita.
    if (!cpuMax.good()) {
        std::cerr << "Falha ao escrever no arquivo cpu.max\n";
        return false;
    }

    return true; // Limite aplicado com sucesso.
}

// define limite de memória escrevendo em memory.max (bytes)
bool CGroupManager::setMemoryLimit(const std::string& name, size_t bytes) {
    std::ofstream memMax(basePath + name + "/memory.max"); // abre memory.max
    if (!memMax.is_open()) return false;                   // falha se não abriu
    memMax << bytes;                                       // escreve o número de bytes (ex: 100000000)
    return true;
}

// lê cpu.stat e devolve um mapa de pares chave->valor (double)
std::map<std::string, double> CGroupManager::readCpuUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/cpu.stat");    // abre cpu.stat
    std::map<std::string, double> stats;               // mapa para guardar os pares
    std::string key;
    double val;

    while (f >> key >> val) {                          // lê até EOF pares "key value"
        stats[key] = val;                              // armazena no mapa (substitui se já existir)
    }
    return stats;                                      // retorna mapa (pode estar vazio se arquivo não existir)
}

// lê memory.current (apenas um número) e retorna em um mapa com chave "memory.current"
std::map<std::string, size_t> CGroupManager::readMemoryUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/memory.current"); // abre memory.current
    std::map<std::string, size_t> stats;
    size_t val;
    if (f >> val)                                         // se conseguiu ler um valor
        stats["memory.current"] = val;                    // guarda sob a chave "memory.current"
    return stats;
}

// lê io.stat e devolve uma lista de BlkIOStats (uma struct com campos para rbytes, wbytes, etc.)
std::vector<BlkIOStats> CGroupManager::readBlkIOUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/io.stat"); // abre io.stat
    std::vector<BlkIOStats> list;

    if (!f.is_open()) {                             // se não abriu, informa e retorna vetor vazio
        std::cerr << "Falha ao abrir io.stat\n";
        return list;
    }

    std::string line;

    while (std::getline(f, line)) {                 // lê linha por linha
        if (line.empty()) continue;                 // ignora linhas vazias

        BlkIOStats s{};                             // inicializa struct (zeros)
        std::istringstream iss(line);               // cria stream a partir da linha

        // formato esperado:
        // 8:0 rbytes=123 wbytes=456 rios=3 wios=4 dbytes=0 dios=0
        iss >> s.major;                             // lê major (número antes do ':')
        iss.ignore(1); // ':'                        // descarta o caractere ':' que separa major:minor
        iss >> s.minor;                             // lê minor (número após ':')

        std::string kv;
        while (iss >> kv) {                         // lê tokens do tipo key=value
            auto pos = kv.find('=');                // encontra o '='
            if (pos == std::string::npos) continue; // se não encontrou '=', pula
            std::string key = kv.substr(0, pos);    // chave (ex: "rbytes")
            uint64_t val = std::stoull(kv.substr(pos + 1)); // converte a parte após '=' para inteiro

            if (key == "rbytes") s.rbytes = val;    // preenche o campo adequado na struct
            else if (key == "wbytes") s.wbytes = val;
            else if (key == "rios") s.rios = val;
            else if (key == "wios") s.wios = val;
            else if (key == "dbytes") s.dbytes = val;
            else if (key == "dios") s.dios = val;
        }

        list.push_back(s);                          // adiciona a entrada do dispositivo ao vetor
    }

    return list;                                    // retorna o vetor com estatísticas de I/O
}

// ===== Experimento 3: testar throttling de CPU =====
void CGroupManager::runCpuThrottlingExperiment() {
    std::string cg = "exp3_" + std::to_string(time(nullptr)); // nome único do cgroup usando timestamp
    this->createCGroup(cg);                                  // cria o cgroup

    std::cout << "\n===== EXPERIMENTO 3 — CPU THROTTLING =====\n";

    pid_t pid = fork();                                      // cria um processo filho
    if (pid < 0) {                                           // verifica erro no fork
        std::cerr << "fork falhou\n";
        return;                                              // aborta experimento se fork falhar
    }

    if (pid == 0) {                                          // === bloco do FILHO ===
        CGroupManager mgr(this->basePath);                   // cria uma instância local para operar no mesmo basePath
        mgr.moveProcessToCGroup(cg, getpid());               // move o próprio filho para o cgroup

        while (true) {
            asm volatile("" ::: "memory");                   // loop infinito ocupando CPU (busy spin)
        }

        exit(0);                                             // unreachable (só fica como fallback)
    }

    // === bloco do PAI ===
    std::vector<double> limites = { 0.25, 0.5, 1.0, 2.0 };    // limites que serão testados (em "cores")

    for (double lim : limites) {
        this->setCpuLimit(cg, lim);                          // aplica o limite no cgroup

        auto t0 = std::chrono::high_resolution_clock::now();// marca tempo inicial
        std::this_thread::sleep_for(std::chrono::seconds(2)); // espera 2 segundos (período de medição)
        auto t1 = std::chrono::high_resolution_clock::now();// marca tempo final

        double secs = std::chrono::duration<double>(t1 - t0).count(); // duração em segundos

        auto stat = this->readCpuUsage(cg);                  // lê cpu.stat (mapa de métricas)
        if (!stat.count("usage_usec")) {                     // valida se usage_usec existe
            std::cerr << "cpu.stat não contém usage_usec\n";
            continue;                                       // pula essa iteração se não existir
        }
        double usage_sec = stat["usage_usec"] / 1e6;         // uso de CPU em segundos (cpu.stat fornece microssegundos)

        double cpuPercent = (usage_sec / secs) * 100.0;      // percentagem de CPU consumida no intervalo medido
        double expected = lim * 100.0;                      // valor esperado (lim em núcleos * 100)
        double desvio = ((cpuPercent - expected) / expected) * 100.0; // desvio percentual relativo

        std::cout << "\n--- Limite: " << lim << " cores ---\n";
        std::cout << "CPU medido: " << cpuPercent << "%\n";
        std::cout << "CPU esperado: " << expected << "%\n";
        std::cout << "Desvio: " << desvio << "%\n";
    }

    kill(pid, SIGKILL);                                     // mata o processo filho (força)
    int status = 0;
    waitpid(pid, &status, 0);                               // espera o filho terminar e recolhe status
}

// ===== Experimento 4: testar limite de memória =====
void CGroupManager::runMemoryLimitExperiment() {
    std::string cg = "exp4_" + std::to_string(time(nullptr)); // nome único do cgroup
    this->createCGroup(cg);                                  // cria o cgroup

    // Limite fixo de 100 MB
    this->setMemoryLimit(cg, 100ull * 1024 * 1024);          // escreve 100 * 1024 * 1024 bytes em memory.max

    std::cout << "\n===== EXPERIMENTO 4 — LIMITE DE MEMÓRIA (cgroups v2) =====\n";

    pid_t pid = fork();                                      // cria filho
    if (pid < 0) {                                           // (opcional) checar erro no fork
        std::cerr << "fork falhou\n";
        return;
    }

    if (pid == 0) {                                          // === FILHO ===
        CGroupManager mgr(this->basePath);                   // instância local para manipular cgroup
        mgr.moveProcessToCGroup(cg, getpid());               // move o próprio filho para o cgroup

        std::vector<void*> blocos;                           // vetor para guardar ponteiros alocados
        size_t total = 0;                                    // total alocado
        const size_t passo = 20 * 1024 * 1024; // 20 MB    // tamanho do bloco alocado por iteração

        while (true) {
            void* b = malloc(passo);                         // tenta alocar 20 MB
            if (!b) break;    // Caso o malloc falhe, encerramos // malloc retorna NULL se não houver memória
            memset(b, 0, passo);                             // escreve em toda a memória para garantir alocação física

            blocos.push_back(b);                             // guarda ponteiro para liberar depois (não libera aqui)
            total += passo;                                  // atualiza contagem

            auto mem = mgr.readMemoryUsage(cg);              // lê memory.current do cgroup

            std::cout << "Alocado: " << (total / (1024 * 1024))
                << " MB | memory.current=" << mem["memory.current"]
                << "\n";

            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // pequena pausa entre alocações
        }

        exit(0); // Só acontece se o filho terminar sem OOM kill — se OOM killer matar, parent verá sinal
    }

    // === PAI ===
    int status;
    waitpid(pid, &status, 0);                                // espera o filho terminar (OOM kill ou exit)

    // Ler memory.events (contadores gerados pelo kernel)
    size_t oom = 0, oom_kill = 0, high = 0;
    {
        std::ifstream fe(this->basePath + cg + "/memory.events"); // abre memory.events
        std::string k;
        size_t v;
        while (fe >> k >> v) {                                 // form: "<key> <value>" por linha
            if (k == "oom")      oom = v;                      // atualiza contadores correspondentes
            if (k == "oom_kill") oom_kill = v;
            if (k == "high")     high = v;
        }
    }

    // Ler memory.peak (pico de uso medido pelo kernel)
    size_t peak = 0;
    {
        std::ifstream fp(this->basePath + cg + "/memory.peak");
        if (fp) fp >> peak;                                    // se o arquivo existir, lê o valor
    }

    std::cout << "\n===== RESULTADO FINAL =====\n";
    std::cout << "oom       = " << oom << "\n";                  // número de eventos oom
    std::cout << "oom_kill  = " << oom_kill << "\n";            // número de kills pelo oom_killer
    std::cout << "high      = " << high << "\n";                // número de eventos high (pressionamento de memória)
    std::cout << "Máximo de memória alcançada = " << peak / (1024 * 1024) << " MB\n"; // pico em MB
}

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

        // Habilita memory para subgrupos
        std::ofstream subtree(basePath + "cgroup.subtree_control");
        if (subtree.is_open()) {
            subtree << "+memory +cpu +io +pids";
        }

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

// Define limite de memória (cgroup v2) escrevendo em memory.max. Recebe como parâmetro o nome do cgroup e a memória em bytes que quer setar
bool CGroupManager::setMemoryLimit(const std::string& name, size_t bytes) {
    std::string path = basePath + name + "/memory.max"; // Define o caminho do arquivo que seta a memória do cgroup

    // Verifica se o arquivo existe. Caso não exista, mostra ao usuário
    if (!fs::exists(path)) {
        std::cerr << "Erro: " << path << " não existe\n";
        return false;
    }

    // Abre o arquivo para escrita. Caso não consiga, mostra ao usuário
    std::ofstream memMax(path); 
    if (!memMax.is_open()) {
        std::cerr << "Erro ao abrir " << path << " para escrita.\n";
        return false;
    }

    // Valida entrada, evita tamanho 0 e ajusta para 1 se for preciso
    if (bytes == 0) {
        std::cerr << "Aviso: bytes = 0 bloquearia toda memória. Ajustando para 1.\n";
        bytes = 1; // Set de bytes em 1
    }

    // Escreve o limite no arquivo
    memMax << bytes; 
    memMax.flush(); // .flush() garante que os dados sejam escritos no arquivo.

    // Verifica se a escrita falhou. Caso tenha falhado, mostra ao usuário
    if (!memMax.good()) {
        std::cerr << "Falha ao escrever em memory.max\n";
        return false;
    }

    return true; // Limite aplicado com sucesso.
}

// Lê o arquivo cpu.stat e retorna um mapa com métricas (chave -> valor)
std::map<std::string, double> CGroupManager::readCpuUsage(const std::string& name) {
    // Monta o caminho completo do arquivo cpu.stat no cgroup
    const std::string path = basePath + name + "/cpu.stat";

    // Abre o arquivo cpu.stat para leitura
    std::ifstream f(path);

    // Se não abriu, imprime erro e retorna mapa vazio
    if (!f.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << path << "\n";
        return {}; // Mapa vazio indica falha
    }

    // Mapa onde serão armazenados os pares "chave valor"
    std::map<std::string, double> stats;

    // Armazena cada linha lida do arquivo
    std::string line;

    // Lê o arquivo linha a linha
    while (std::getline(f, line)) {

        // Ignora linhas vazias ou apenas com espaços
        if (line.empty()) continue;

        // Transforma a linha em um stream para processar tokens
        std::istringstream iss(line);

        // Variáveis para armazenar a chave e o valor
        std::string key;
        double value;

        // Tenta ler no formato:  key   value
        if (!(iss >> key >> value)) {
            // Linha inválida
            std::cerr << "Aviso: linha mal formatada em cpu.stat: " << line << "\n";
            continue; // continua lendo as próximas linhas
        }

        // Salva no mapa: substitui se a chave já existir
        stats[key] = value;
    }

    // Retorna o mapa com as métricas lidas
    return stats;
}

// Lê o arquivo memory.stat e retorna um mapa com métricas (chave -> valor)
std::map<std::string, size_t> CGroupManager::readMemoryUsage(const std::string& name) {
    // Monta o caminho completo do arquivo memory.current no cgroup
    const std::string path = basePath + name + "/memory.current";

    // Abre o arquivo memory.stat para leitura
    std::ifstream f(path); 

    // Se não abriu, imprime erro e retorna mapa vazio
    if (!f.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << path << "\n";
        return {}; // Mapa vazio indica falha
    }

    // Mapa onde serão armazenados os pares "chave valor"
    std::map<std::string, size_t> stats;

    // Variável que receberá a informação para ser mapeada
    size_t value = 0; // Inicializa como 0

    // Verifica se há alguma valor em memory.current
    if (f >> value) {
        stats["memory.current"] = value;  // Armazena cada métrica com seu devido valor
    }

    // Retorna a mapa das métricas e valores da memória
    return stats;
}

// Função da clsse do CGroupManager que faz a leitura de BlkIOUsage. Recebe como parâmetro o nome do cgroup
std::vector<BlkIOStats> CGroupManager::readBlkIOUsage(const std::string& name) {
    // Abre o arquivo io.stat do cgroup especificado
    std::ifstream f(basePath + name + "/io.stat");

    // Vetor que vai armazenar todas as estatísticas encontradas
    std::vector<BlkIOStats> list;

    // Caso não consiga abrir o arquivo, retorna lista vazia
    if (!f.is_open()) {
        std::cerr << "Falha ao abrir io.stat\n"; // Mostra mensagem ao usuário 
        return list;
    }

    // Reserva espaço para algumas entradas (otimização)
    list.reserve(8); // Geralmente há poucos dispositivos por cgroup

    std::string line; 

    // Lê o arquivo linha por linha
    while (std::getline(f, line)) {
        // Ignora linhas vazias, indo para a próxima interação
        if (line.empty())
            continue;

        // Transforma a linha em um stream para extrair tokens
        std::istringstream iss(line);

        // Estrutura que vai armazenar os valores da linha atual. Declarada no arquivo header
        BlkIOStats s{};

        // Lê o primeiro campo: major:minor ou "Default"
        std::string device;
        iss >> device;

        // Procura ":" para identificar o dispositivo major/minor
        auto colon = device.find(':');
        if (colon != std::string::npos) { // Verifica se é como exemplo 8:0
            try {
                // Converte parte antes dos dois pontos para major
                s.major = std::stoul(device.substr(0, colon));
                // Converte parte após os dois pontos para minor
                s.minor = std::stoul(device.substr(colon + 1));
            }
            catch (...) {
                continue; // Se algo deu errado, ignora toda a linha
            }
        }
        else {
            // Caso seja "Default": representa agregação de dispositivos
            s.major = s.minor = 0;
        }

        // Lê todos os campos no formato key=value
        std::string kv;
        while (iss >> kv) {
            // Procura o '=' que separa chave e valor
            auto pos = kv.find('=');
            if (pos == std::string::npos)
                continue; // token inválido, ignora e vai para a próxima intereção

            // Extrai chave e valor
            std::string key = kv.substr(0, pos);
            std::string valStr = kv.substr(pos + 1);

            uint64_t val = 0; // Inicializa a variável que vai receber o valor
            try {
                val = std::stoull(valStr); // Converte valor para número
            }
            catch (...) {
                continue; // Valor inválido
            }

            // Armazena o valor na estrutura de BlkIO
            if (key == "rbytes") s.rbytes = val;
            else if (key == "wbytes") s.wbytes = val;
            else if (key == "rios")   s.rios = val;
            else if (key == "wios")   s.wios = val;
            else if (key == "dbytes") s.dbytes = val;
            else if (key == "dios")   s.dios = val;
        }

        // Adiciona a estrutura preenchida ao vetor final
        list.push_back(s);
    }

    // Retorna todas as estatísticas lidas
    return list;
}

uint64_t CGroupManager::readIterationsFromChild(pid_t pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream f(path);
    if (!f.is_open()) {
        return 0;
    }

    std::string token;
    std::vector<std::string> fields;

    while (f >> token) {
        fields.push_back(token);
    }

    if (fields.size() < 15) {
        return 0;
    }

    uint64_t utime = std::stoull(fields[13]); // campo 14
    uint64_t stime = std::stoull(fields[14]); // campo 15

    return utime + stime;
}

// ===== Experimento 3: testar throttling de CPU =====
void CGroupManager::runCpuThrottlingExperiment() {

    std::string cg = "exp3_" + std::to_string(time(nullptr));
    this->createCGroup(cg);

    std::cout << "\n===== EXPERIMENTO 3 — CPU THROTTLING =====\n";

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork falhou\n";
        return;
    }

    // =====================================================================
    // === FILHO ===
    // =====================================================================
    if (pid == 0) {

        // mover o próprio processo para o cgroup
        CGroupManager mgr(this->basePath);
        mgr.moveProcessToCGroup(cg, getpid());

        // processo CPU-bound (busy loop puro)
        while (true) {
            asm volatile(""); // evita otimizações do compilador
        }

        exit(0); // nunca chega aqui
    }

    // =====================================================================
    // === PAI ===
    // =====================================================================
    std::vector<double> limites = { 0.25, 0.5, 1.0, 2.0 };

    for (double lim : limites) {

        // Aplicar limite
        this->setCpuLimit(cg, lim);

        // Deixar estabilizar
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // -----------------------------------------------------------------
        // LEITURA INICIAL (usage_usec + ticks do processo)
        // -----------------------------------------------------------------

        auto stat_before = this->readCpuUsage(cg);
        if (!stat_before.count("usage_usec")) {
            std::cerr << "cpu.stat não contém usage_usec\n";
            continue;
        }

        double usage_before = stat_before["usage_usec"] / 1e6; // micros → segundos
        uint64_t ticks_before = this->readIterationsFromChild(pid);

        auto t0 = std::chrono::high_resolution_clock::now();

        // Janela de medição (2 segundos)
        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto t1 = std::chrono::high_resolution_clock::now();

        // -----------------------------------------------------------------
        // LEITURA FINAL
        // -----------------------------------------------------------------

        auto stat_after = this->readCpuUsage(cg);
        double usage_after = stat_after["usage_usec"] / 1e6;

        uint64_t ticks_after = this->readIterationsFromChild(pid);

        // -----------------------------------------------------------------
        // Cálculos de delta
        // -----------------------------------------------------------------

        double cpu_used = usage_after - usage_before;
        double secs = std::chrono::duration<double>(t1 - t0).count();

        // CPU efetiva (%) = (tempo de CPU usado / tempo de parede) * 100
        double cpuPercent = (cpu_used / secs) * 100.0;

        // Throughput em ticks/segundo
        uint64_t ticks = ticks_after - ticks_before;
        double throughput = ticks / secs;

        // Cálculo do desvio
        double expected = lim * 100.0;
        double desvio = ((cpuPercent - expected) / expected) * 100.0;

        // -----------------------------------------------------------------
        // Imprimir resultados
        // -----------------------------------------------------------------

        std::cout << "\n--- Limite: " << lim << " cores ---\n";
        std::cout << "CPU medido:       " << cpuPercent << "%\n";
        std::cout << "CPU esperado:     " << expected << "%\n";
        std::cout << "Desvio:           " << desvio << "%\n";
        std::cout << "Throughput (ticks/s): " << throughput << "\n";
    }

    // Encerrar filho
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
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

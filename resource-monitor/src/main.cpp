#include "cgroup.h"
#include "monitor.h"
#include "namespace.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <vector>
#include <limits>
#include <algorithm>
#include <thread>
#include <chrono>
#include <set>
#include <unistd.h>
#include <iomanip>
#include <cmath>
#include <sys/wait.h>
#include <fcntl.h>  

namespace fs = std::filesystem;

struct ProcessInfo {
	int pid;
	std::string name;
};

bool processoExiste(int PID) {//Checa se o processo existe de fato
	std::filesystem::path dir = "/proc/" + std::to_string(PID);//concatena o path do processo
	return std::filesystem::exists(dir) && std::filesystem::is_directory(dir);//Checa existência
}

void salvarMedicoesCSV(const StatusProcesso& medicao, const calculoMedicao& calculado)
{
    // Obtém o diretório atual do programa
    std::filesystem::path base = std::filesystem::current_path();
    // Constrói o path completo para o arquivo CSV: [base]/docs/dados[PID].csv
    std::filesystem::path path = base / "docs" / ("dados" + std::to_string(medicao.PID) + ".csv");

    // Verifica se o arquivo é novo (não existe ou está vazio)
    bool novo = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;

    // Abre o arquivo em modo append (adiciona no final)
    std::ofstream f(path, std::ios::app);
    if (!f) return; // Sai se não conseguiu abrir o arquivo

    // Se o arquivo é novo, escreve o cabeçalho CSV
    if (novo) {
        f << "timestamp,PID,utime,stime,threads,contextSwitchfree,contextSwitchforced,"
            "vmSize_kB,vmRss_kB,vmSwap_kB,minfault,mjrfault,bytesLidos,bytesEscritos,"
            "rchar,wchar,syscallLeitura,syscallEscrita,"
            "usoCPU_pct,usoCPUGlobal_pct,taxaLeituraDisco_KiB_s,taxaLeituraTotal_KiB_s,"
            "taxaEscritaDisco_KiB_s,taxaEscritaTotal_KiB_s\n";
    }

    // Obtém timestamp UTC atual
    std::time_t t = std::time(nullptr); // tempo atual em segundos desde epoch
    std::tm tm;
    gmtime_r(&t, &tm); // converte para UTC
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm); // formata em ISO 8601

    // Escreve uma linha no CSV com os dados do processo e métricas calculadas
    f << ts << ","                            // timestamp
        << medicao.PID << ","                 // PID do processo
        << medicao.utime << ","               // tempo CPU usuário (s)
        << medicao.stime << ","               // tempo CPU kernel (s)
        << medicao.threads << ","             // número de threads
        << medicao.contextSwitchfree << ","   // context switches voluntários
        << medicao.contextSwitchforced << "," // context switches forçados
        << medicao.vmSize << ","               // tamanho virtual (kB)
        << medicao.vmRss << ","                // memória residente (kB)
        << medicao.vmSwap << ","               // swap usado (kB)
        << medicao.minfault << ","             // page faults menores
        << medicao.mjrfault << ","             // page faults maiores
        << medicao.bytesLidos << ","           // bytes lidos do disco
        << medicao.bytesEscritos << ","        // bytes escritos no disco
        << medicao.rchar << ","                // bytes lidos via syscall read
        << medicao.wchar << ","                // bytes escritos via syscall write
        << medicao.syscallLeitura << ","       // número de syscalls read
        << medicao.syscallEscrita << ","       // número de syscalls write
        << calculado.usoCPU << ","             // CPU % do processo
        << calculado.usoCPUGlobal << ","       // CPU % global
        << calculado.taxaLeituraDisco << ","   // taxa leitura disco (KiB/s)
        << calculado.taxaLeituraTotal << ","   // taxa leitura total (KiB/s)
        << calculado.taxaEscritaDisco << ","   // taxa escrita disco (KiB/s)
        << calculado.taxaEscritaTotal          // taxa escrita total (KiB/s)
        << "\n";                               // fim de linha
}

std::vector<ProcessInfo> listarProcessos() {
	std::vector<ProcessInfo> lista;

	for (const auto& entry : fs::directory_iterator("/proc")) {
		if (!entry.is_directory()) continue;

		const std::string dir = entry.path().filename().string();
		if (!std::all_of(dir.begin(), dir.end(), ::isdigit)) continue;

		int pid = std::stoi(dir);
		std::ifstream cmdline("/proc/" + dir + "/comm");
		std::string nome;
		if (cmdline.good())
			std::getline(cmdline, nome);

		if (!nome.empty())
			lista.push_back({ pid, nome });
	}
	return lista;
}

int escolherPID() {
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

	auto processos = listarProcessos();

	std::cout << "\n\033[1;33m=================== Processos disponiveis ==================\033[0m\n";
	for (const auto& p : processos) {
		std::cout << "PID: " << std::left << std::setw(25) << p.pid << "\tNome: " << p.name << "\n";
	}

	int pid;
	bool valido = false;

	do {
		std::cout << "\nDigite o número do PID escolhido: ";
		std::cin >> pid;

		if (std::cin.fail()) {
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			std::cout << "Entrada inválida. Tente novamente.\n";
			continue;
		}

		for (const auto& p : processos) {
			if (p.pid == pid) {
				if (processoExiste(pid)) {
					valido = true;
					break;
				}
			}
		}

		if (!valido)
			std::cout << "PID não encontrado entre os processos listados.\n";

	} while (!valido);

	return pid;
}

void executarExperimentos() {
	CGroupManager manager;

	int sub = -1;
	do {
		std::cout << "\n\033[1;36m======================= EXPERIMENTOS =======================\033[0m\n";
		std::cout << "\n\033[1;33m========================== CGROUP ==========================\033[0m\n";
		std::cout << "\033[1m"; // deixa opções em negrito
		std::cout << " 1. Experimento nº3 – Throttling de CPU\n";
		std::cout << " 2. Experimento nº4 – Limite de Memória\n";
		std::cout << "\033[0m";
		std::cout << "\n\033[1;33m======================== NAMESPACE =========================\033[0m\n";
		std::cout << "\033[1m"; // deixa opções em negrito
		std::cout << " 3. Experimento nº2 - Isolamento via Namespaces\n";
		std::cout << "\033[0m";
		std::cout << "\n\033[1;33m======================== PROFILER ==========================\033[0m\n";
		std::cout << "\033[1m"; // deixa opções em negrito
		std::cout << " 4. Experimento nº1 - Overhead de Monitoramento\n";
		std::cout << " 5. Experimento nº5 - Limitação de I/O\n\n";
		std::cout << " 0. Voltar ao menu principal.\n";
		std::cout << " Escolha: ";
		std::cout << "\033[0m";
		std::cin >> sub;

		if (std::cin.fail()) {
			std::cin.clear(); // Limpa a entrada
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpa o buffer de entrada
			std::cerr << "Opção Inválida. Por favor, digite apenas números válidos.\n"; // Mostra mensagem ao usuário
			sub = -1;
			continue;
		}
		else if (sub == 1) {
			manager.runCpuThrottlingExperiment();
		}
		else if (sub == 2) {
			manager.runMemoryLimitExperiment();
		}
		else if (sub == 3) {
			executarExperimentoIsolamento();
		}
		else if (sub == 4) {
			overheadMonitoramento();
		}
		else if (sub == 5) {
			limitacaoIO();
		}
		else if (sub != 0) {
			std::cout << "Opção inválida.\n";
		}
	} while (sub != 0);
}

// Função que cria um processo do tipo IO e move para o cgroup.
// Recebe como parâmetros o nome do cgroup e o objeto da classe CGroupManager
pid_t createIOTestProcessAndMove(const std::string& cgName, CGroupManager& manager) {
    pid_t pid = fork(); // Cria um processo filho usando fork()
    // Retorno: > 0 = PID do filho (no pai), 0 = esta no filho, < 0 = erro

    // Verifica se o PID gerado do processo filho é menor que 0
    if (pid < 0) {
        perror("fork"); // Imprime no stderr uma mensagem com o errno atual: "fork: <mensagem do sistema>"
        return -1;      // Indica erro ao chamador retornando -1
    }

    // Se pid == 0, esta no processo filho
    if (pid == 0) {
        // Redireciona stdout e stderr para /dev/null (evita poluir o terminal)
        int devNull = open("/dev/null", O_WRONLY); // Abre /dev/null para escrita
        if (devNull >= 0) {                        // Se open teve sucesso
            dup2(devNull, STDOUT_FILENO);          // Faz stdout apontar para /dev/null
            dup2(devNull, STDERR_FILENO);          // Faz stderr apontar para /dev/null
            close(devNull);                        // Fecha o descritor original (dup2 criou duplicatas)
        }

        const size_t SIZE = 1024 * 1024;          // Define o tamanho do bloco de escrita: 1 MB
        std::vector<char> buffer(SIZE, 'X');      // Aloca e preenche um buffer de 1 MB com o caractere 'X'

        // Abre (ou cria) o arquivo de teste em /tmp. O_TRUNC zera o arquivo ao abrir.
        int fd = open("/tmp/test_io_file", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {                              // Se falhou ao abrir/criar
            perror("open");                        // Mostra erro
            _exit(1);                              // Sai imediatamente do filho (sem handlers do pai)
        }

        // === Primeira escrita imediata (garante blkio na 1ª leitura) ===
        ssize_t written = write(fd, buffer.data(), SIZE); // Tenta escrever 1 MB no arquivo
        if (written < 0) {                         // Se houve erro na escrita
            perror("write");                       // Imprime motivo
            close(fd);                             // Fecha descritor
            _exit(1);                              // Sai do processo filho com código 1
        }
        fsync(fd);                                 // Força a sincronização dos dados para o dispositivo (bloqueante)

        // === Loop 1MB/s ===
        while (true) {                             // Loop infinito: gera I/O continuamente
            sleep(1);                              // Pausa 1 segundo (taxa aproximada de 1 MB/s)
            written = write(fd, buffer.data(), SIZE); // Escreve mais 1 MB
            if (written < 0) {                     // Se write retornou erro
                perror("write");                   // Imprime erro
                close(fd);                         // Fecha o descritor
                _exit(1);                          // Sai do filho
            }
            fsync(fd);                             // Sincroniza novamente (garante I/O físico)
        }
    }

    // Código executado apenas no processo PAI 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Pequena pausa para dar tempo do filho criar o arquivo/começar a escrever antes de movê-lo ao cgroup

    try {
        manager.moveProcessToCGroup(cgName, pid); // Move o PID do filho para o cgroup especificado
    }
    catch (const std::exception& ex) {
        std::cerr << "Erro ao mover processo p/ cgroup: " << ex.what() << "\n"; // Trata exceção lançada pelo manager
    }

    return pid; // Retorna o PID do processo filho ao chamador (no pai)
}

// Função que faz o gerenciado do cgroup no main
void cgroupManager() {
    // Cria uma instância do gerenciador de cgroups (objeto que encapsula operações com cgroups).
    CGroupManager manager;

    // Imprime um cabeçalho colorido no terminal (código ANSI para cor ciano em negrito).
    std::cout << "\033[1;36m";
    std::cout << "\n============================================================\n";
    std::cout << "                         CGroup Manager                      \n";
    std::cout << "============================================================\n";
    std::cout << "\033[0m";

    // Gera um nome único para o cgroup usando o horário atual, para sempre criar nomes diferentes.
    std::string cgroupName = "exp_" + std::to_string(time(nullptr));
    std::cout << "Nome do cgroup experimental: " << cgroupName << ".\n";

    // Tenta criar o cgroup; se falhar, imprime erro e retorna.
    if (!manager.createCGroup(cgroupName)) {
        std::cerr << "Falha ao criar cgroup.\n";
        return;
    }

    int pid = -1; // Set do pid como -1 
    int opc = 0; // Set da opção padrão como 0
 
    // Validação se o usuário digitou 1 ou 2. Só sai do loop se digitar um valor válido
    while (true) {
        // Mostra opções ao usuário de qual tipo de processo deseja usar 
        std::cout << " 1. Escolher processo existente\n";
        std::cout << " 2. Criar processo de teste para I/O\n";
        std::cout << " 0. Voltar ao menu principal.\n";
        std::cout << "Escolha: ";

        std::cin >> opc; // Armazena a opção na variável de opção

        // Se o input for inválido, caso seja letras
        if (std::cin.fail()) {
            std::cin.clear(); // Limpa a entrada
            std::cin.ignore(10000, '\n'); // Descarta caracteres pendentes no buffer
            std::cerr << "Entrada inválida. Digite 1 ou 2.\n\n"; // Mostra mensagem ao usuário
            continue;
        }

        if (opc == 1) { // Caso tenha digitado 1, o usuário que deve escolher o processo existente
            pid = escolherPID(); // Chama a função de escolher o PID, que acessa a pasta /proc e lista os processos 
            if (!manager.moveProcessToCGroup(cgroupName, pid)) { // Move o processo para o cgroup usando moveProcessToCGroup da classe CGroupManager
                std::cerr << "Falha ao mover o processo para o cgroup.\n"; // Caso ocorra um erro mostra mensagem ao usuário
                return;
            }
            std::cout << "Processo " << pid << " movido para " << cgroupName << ".\n"; // Se funcionou, mostra o PID que foi movido
            break; // Sai do loop
        }
        else if (opc == 2) { // Caso tenha digitado 2, é criado um processo de IO e movido para o cgroup
            pid = createIOTestProcessAndMove(cgroupName, manager); // Cria o processo IO e move para o cgroup
            if (pid <= 0) { // Caso tenha retornado um PID <= 0 significa que deu erro
                std::cerr << "Falha ao criar processo de I/O!\n"; // Mostra mensagem ao usuário
                return;
            }
            break; // Sai do loop
        }
        else if (opc == 0) { // Caso tenha digitado 0, volta ao menu principal 
            return;
        }
        else {
            std::cerr << "Opção inválida. Digite 1 ou 2.\n\n"; // Se o usuário digitou algul número diferente de 1 ou 2 mostra mensagem
        }
    }

    // Lê do usuário o limite de CPU (em "núcleos" — valor float/double; -1 significa ilimitado).
    double cores; // Set da variável que recebe a quantidade de cores
    std::cout << "Limite de CPU (em núcleos, ex: 0.5, 1.0, -1 para ilimitado): ";
    std::cin >> cores;

    // Faz o set do limite da Cpu e verifica se deu certo. Manda como parâmetros o nome do cgroup e a quantidade de cores
    if (!manager.setCpuLimit(cgroupName, cores)) {
        std::cerr << "Falha ao limitar a CPU.\n"; // Caso ocorra um erro mostra o problema ao usuário
        return;
    }
    std::cout << "CPU limitada em " << cores << " cores.\n"; // Mostra a mensagem da CPU limitada ao usuário

    // Processo de fazer o set da memória 
    size_t memBytes; // Set da variável que recebe a memória em bites
    std::cout << "Limite de memória (em bytes, ex: 1000000000): ";
    std::cin >> memBytes;

    // Faz o set do limite de memória e verifica se deu certo. Manda como parâmetros o nome do cgroup e a memória em bytes
    if (!manager.setMemoryLimit(cgroupName, memBytes)) {
        std::cerr << "Falha ao limitar a memória.\n"; // Caso ocorra um erro mostra o problema ao usuário
        return;
    }
    std::cout << "Memória limitada em " << memBytes << " bytes.\n"; // Mostra a mensagem da memória limitada ao usuário

    std::cout << "\n--- Monitorando o processo ---\n";

    int numeroLeitura = 1; // Set da variável do número da leitura
    while (numeroLeitura <= 10) { // Loop que faz as 5 leituras 
        // Verifica se o processo ainda está rodando
        if (kill(pid, 0) != 0) { // kill(pid, 0) não mata o processo, apenas testa existência
            std::cout << "\nProcesso " << pid << " finalizou. Encerrando monitoramento.\n";
            break;
        }

        // Começa a gerar o relatório de uso da cgroup, mapeando CPU, memória e BlkIO
        std::cout << "\n--- Relatório de uso (leitura " << numeroLeitura << ") ---\n";

        // Chama a função que faz a leitura do uso da CPU e armazena em cpu
        auto cpu = manager.readCpuUsage(cgroupName);

        // Se o mapa está vazio, pode significar erro ao abrir cpu.stat
        if (cpu.empty()) {
            std::cout << "Nenhum dado de CPU disponível (cpu.stat ausente ou vazio).\n";
        }
        else { // Caso contenha informações
            if (cpu.count("usage_usec")) // Mostra o total de CPU usada
                std::cout << "CPU total usada (µs): " << cpu["usage_usec"] << "\n";
            else
                std::cout << "usage_usec não encontrado no cpu.stat\n";

            if (cpu.count("user_usec")) // Mostra o total de CPU usada em modo usuário
                std::cout << "Tempo em modo usuário (µs): " << cpu["user_usec"] << "\n";
            else
                std::cout << "user_usec não encontrado no cpu.stat\n";

            if (cpu.count("system_usec")) // Mostra o total de CPU usada em modo kernel
                std::cout << "Tempo em modo kernel (µs): " << cpu["system_usec"] << "\n";
            else
                std::cout << "system_usec não encontrado no cpu.stat\n";
        }

        // Chama a função que faz a leitura do uso da memória e armazena em mem
        auto mem = manager.readMemoryUsage(cgroupName);

        // memory.current  → uso atual (obrigatório em cgroup v2)
        if (mem.count("memory.current"))
            std::cout << "Memória atual: "
            << mem["memory.current"] << " bytes\n";
        else
            std::cout << "Não foi possível ler a memória atual\n";

        std::cout << "\nEstatísticas de BlkIO:\n";

        // Armazena em blk a leitura de BlkIO usando a função da classe CGroupManager readBlkIOUsage()
        auto blk = manager.readBlkIOUsage(cgroupName);

        bool encontrouAlgo = false;  // Marca se encontra alguma linha útil

        // Loop que percorre cada entrada de blk
        for (const auto& entry : blk) {

            // Se todas as métricas forem zero, segue para a próxima leitura
            if (entry.rbytes == 0 && 
                entry.wbytes == 0 &&
                entry.dbytes == 0 &&
                entry.rios == 0 &&
                entry.wios == 0 &&
                entry.dios == 0)
            {
                continue;
            }

            // Encontrou alguma informação de IO, marcando encontrouAlgo como true
            encontrouAlgo = true;

            // Exibe o identificador do dispositivo (0:0 é usado como "Device Default")
            if (entry.major == 0 && entry.minor == 0)
                std::cout << "  Device Default\n";
            else // Caso não seja um dispositivo padrão
                std::cout << "  Device " << entry.major << ":" << entry.minor << "\n";

            // Declaração de um lambda (função anônima) que recebe um uint64_t b (número de bytes) e retorna um sufixo
            auto fmtBytes = [](uint64_t b) {
                const char* suf[] = { "B", "KB", "MB", "GB", "TB" }; // Vetor de sufixos de unidade 
                int i = 0; // Índice no array suf, começando em 0 (unidade "B").
                double v = static_cast<double>(b); // Converte b para double para permitir divisões não inteiras e formatação com casas decimais.
                while (v >= 1024.0 && i < 4) { // Loop que reduz v dividindo por 1024 enquanto ainda for grande o suficiente e enquanto não ultrapassar o último sufixo (TB)
                    v /= 1024.0; // Divide v por 1024 — move para a próxima unidade(B → KB → MB → ...).
                    i++; // Incrementa o índice para usar o próximo sufixo.
                }
                std::ostringstream oss; // Cria um stream de saída em string para montar o texto com formatação.
                oss << std::fixed << std::setprecision(2) << v << " " << suf[i]; // Escreve v com formato fixo e 2 casas decimais, seguido de espaço e o sufixo correspondente.
                return oss.str(); // Retorna a string gerada (ex.: "1.50 MB").
            };

            // Mostra na tela a quantidade de memória lida e a quantidade de operações de leitura realizadas
            std::cout << "    Read:    " << fmtBytes(entry.rbytes)
                << "  (" << entry.rios << " ops)\n";

            // Mostra na tela a quantidade de memória escrita e a quantidade de operações de escrita realizadas
            std::cout << "    Write:   " << fmtBytes(entry.wbytes)
                << "  (" << entry.wios << " ops)\n";

            // Mostra na tela a quantidade de memória descartada e a quantidade de operações de descarte realizadas
            std::cout << "    Discard: " << fmtBytes(entry.dbytes)
                << "  (" << entry.dios << " ops)\n\n";
        }

        // Verifica se não encontrou algo, mostrando mensagem que não encontrou IO nessa leitura
        if (!encontrouAlgo) {
            std::cout << "(nenhuma operação de I/O registrada até agora)\n";
        }

        // A cada iteração aguarda 2 segundos antes de atualizar
        sleep(2);
        numeroLeitura = numeroLeitura + 1; // Incrementa em 1 no numero da leitura 
    }

    std::cout << "Monitoramento de cgroup encerrado.\n"; // Mostra mensagem final do relatório de CGroups

    // Se o processo for de IO e ainda existir, finaliza
    if (pid > 0 && kill(pid, 0) == 0 && opc == 2) {
        std::cout << "Encerrando processo de teste de I/O (PID " << pid << ")...\n";
        kill(pid, SIGKILL); // Mata o processo imediatamente
        waitpid(pid, nullptr, 0); // Aguarda o processo finalizar
        std::cout << "Processo finalizado.\n";
    }
}

// =========================================
// FUNÇÃO PARA O Perfilador de Recursos
// =========================================
void resourceProfiler() {
entrada: // label usado para reiniciar o monitor caso haja erro de coleta
    int PID = escolherPID(); // chama função que retorna PID do processo a ser monitorado
    double intervalo; // intervalo de coleta em segundos

    // Impressão do cabeçalho estilizado do Resource Profiler
    std::cout << "\033[1;36m"; // muda cor para ciano em negrito
    std::cout << "\n============================================================\n";
    std::cout << "                       Resource Profiler                     \n";
    std::cout << "============================================================\n";
    std::cout << "\033[0m"; // reseta cores para o restante do texto

    // Solicita ao usuário o intervalo de monitoramento
    while (true) {
        std::cout << "\nInsira o intervalo de monitoramento, em segundos: ";
        std::cin >> intervalo;
        bool flagInsert = true;

        // Verifica se a entrada foi válida
        if (std::cin.fail()) {
            std::cin.clear(); // limpa estado de erro do cin
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // descarta entrada inválida
            std::cout << "Entrada inválida. Tente novamente.\n";
            flagInsert = false;
        }
        if (flagInsert) {
            break; // sai do loop se a entrada foi válida
        }
    }

    // Inicializa structs para armazenar medições
    StatusProcesso medicaoAnterior;
    StatusProcesso medicaoAtual;
    medicaoAtual.PID = PID; // atribui PID atual
    bool flagMedicao = true; // flag para primeira medição
    int contador = 0; // contador de ciclos de medição

    // Loop principal de monitoramento
    while (true) {

        // Coleta dados de CPU, memória, I/O e rede
        if (!(coletorCPU(medicaoAtual) && coletorMemoria(medicaoAtual) && coletorIO(medicaoAtual) && coletorNetwork(medicaoAtual))) {
            // Se falhar na coleta, reinicia a entrada
            std::cout << "\nFalha ao acessar dados do processo\n";
            std::cout << "Reiniciando...\n";
            goto entrada;
        }
        else {
            // Criação e atualização do CSV
            calculoMedicao resultado;

            // Se for a primeira medição, inicializa métricas com zero
            if (flagMedicao) {
                resultado.usoCPU = 0;
                resultado.usoCPUGlobal = 0;
                resultado.taxaLeituraDisco = 0;
                resultado.taxaLeituraTotal = 0;
                resultado.taxaEscritaDisco = 0;
                resultado.taxaEscritaTotal = 0;
                salvarMedicoesCSV(medicaoAtual, resultado); // salva primeira linha
            }

            if (flagMedicao) {
                std::cout << "Primeira medição detectada... \n";
                std::cout << "Dados e métricas serão mostrados a partir da segunda medição, por favor espere " << intervalo << " segundos... \n";
                flagMedicao = false; // desativa flag de primeira medição
            }
            else {
                // Calcula métricas de CPU
                double tempoCPUAtual = medicaoAtual.utime + medicaoAtual.stime;
                double tempoCPUAnterior = medicaoAnterior.utime + medicaoAnterior.stime;
                double deltaCPU = tempoCPUAtual - tempoCPUAnterior; // tempo CPU decorrido
                double usoCPU = (deltaCPU / intervalo) * 100; // uso percentual do processo
                double usoCPUGlobal = usoCPU / (static_cast<double>(sysconf(_SC_NPROCESSORS_ONLN))); // uso relativo por core

                // Calcula métricas de I/O em KiB/s
                double taxaleituraDisco = (static_cast<double>(medicaoAtual.bytesLidos - medicaoAnterior.bytesLidos) / intervalo) / 1024;
                double taxaleituraTotal = (static_cast<double>(medicaoAtual.rchar - medicaoAnterior.rchar) / intervalo) / 1024;
                double taxaEscritaDisco = (static_cast<double>(medicaoAtual.bytesEscritos - medicaoAnterior.bytesEscritos) / intervalo) / 1024;
                double taxaEscritaTotal = (static_cast<double>(medicaoAtual.wchar - medicaoAnterior.wchar) / intervalo) / 1024;

                // Armazena resultados no struct
                resultado.usoCPU = usoCPU;
                resultado.usoCPUGlobal = usoCPUGlobal;
                resultado.taxaLeituraDisco = taxaleituraDisco;
                resultado.taxaLeituraTotal = taxaleituraTotal;
                resultado.taxaEscritaDisco = taxaEscritaDisco;
                resultado.taxaEscritaTotal = taxaEscritaTotal;

                // Salva no CSV
                salvarMedicoesCSV(medicaoAtual, resultado);

                // Imprime tabela detalhada com as métricas coletadas
                printf(
                    "================================================================================\n"
                    "|                                MEDIÇÃO (Processo %d)                          \n"
                    "================================================================================\n"
                    "| Intervalo de monitoramento: %3.2f segundos                                      |\n"
                    "--------------------------------------------------------------------------------\n"
                    "| CPU                      |            |\n"
                    "-----------------------------------------\n"
                    "| user_time(s)             | %-10.6f |\n"
                    "| system_time(s)           | %-10.6f |\n"
                    "| Uso por core (%%)         |  %-9.5f |\n"
                    "| Uso relativo (%%)         |  %-9.5f |\n"
                    "-----------------------------------------\n"
                    "| Threads / ContextSwitch  |            |\n"
                    "-----------------------------------------\n"
                    "| Threads                  | %-10u |\n"
                    "| voluntary_ctxt_switch    | %-10u |\n"
                    "| nonvoluntary_ctxt_switch | %-10u |\n"
                    "-----------------------------------------\n"
                    "| Memória                  |            |\n"
                    "-----------------------------------------\n"
                    "| VmSize (kB)              | %-10lu |\n"
                    "| VmRSS (kB)               | %-10lu |\n"
                    "| VmSwap (kB)              | %-10lu |\n"
                    "| minor faults             | %-10lu |\n"
                    "| major faults             | %-10lu |\n"
                    "-----------------------------------------\n"
                    "| Syscalls                 |            |\n"
                    "-----------------------------------------\n"
                    "| leituras                 | %-10lu |\n"
                    "| escritas                 | %-10lu |\n"
                    "-----------------------------------------\n"
                    "| Taxas (KiB/s)            |            |\n"
                    "-----------------------------------------\n"
                    "| Leitura disco            | %-10.6f |\n"
                    "| Leitura total (rchar)    | %-10.6f |\n"
                    "| Escrita disco            | %-10.6f |\n"
                    "| Escrita total (wchar)    | %-10.6f |\n"
                    "-----------------------------------------\n"
                    "| Network                  |            |\n"
                    "-----------------------------------------\n"
                    "| Bytes em fila (TX)       | %-10lu  |\n"
                    "| Bytes em fila (RX)       | %-10lu  |\n"
                    "| Conexões ativas          | %-10u  |\n"
                    "=========================================\n"
                    "| Próxima medição em %3.2f segundos...    |\n"
                    "=========================================\n\n\n\n\n",
                    medicaoAtual.PID, intervalo,
                    medicaoAtual.utime, medicaoAtual.stime, usoCPU, usoCPUGlobal,
                    medicaoAtual.threads, medicaoAtual.contextSwitchfree, medicaoAtual.contextSwitchforced,
                    medicaoAtual.vmSize, medicaoAtual.vmRss, medicaoAtual.vmSwap,
                    medicaoAtual.minfault, medicaoAtual.mjrfault,
                    medicaoAtual.syscallLeitura, medicaoAtual.syscallEscrita,
                    taxaleituraDisco, taxaleituraTotal, taxaEscritaDisco, taxaEscritaTotal,
                    medicaoAtual.bytesTxfila, medicaoAtual.bytesRxfila,
                    medicaoAtual.conexoesAtivas,
                    intervalo
                );

            }

            // Incrementa contador de ciclos
            contador++;

            // A cada 5 ciclos, pergunta ao usuário se deseja encerrar
            if (contador == 5) {
                int escolha;
                while (true) {
                    std::cout << "Deseja encerrar o monitoramento? (1 -> sim/0 -> não): \n";
                    std::cin >> escolha;
                    bool flagInsert = true;
                    if (std::cin.fail() || escolha > 1) {
                        std::cin.clear();
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        std::cout << "Entrada inválida. Tente novamente.\n";
                        flagInsert = false;
                    }
                    if (flagInsert) {
                        break; // sai se entrada válida
                    }
                }

                // Se usuário quiser encerrar
                if (escolha == 1) {
                    while (true) {
                        std::cout << "Certo, você quer monitorar outro processo ou sair do resource profiler? (1 -> sair/0 -> outro processo): \n";
                        std::cin >> escolha;
                        bool flagInsert = true;
                        if (std::cin.fail() || escolha > 1) {
                            std::cin.clear();
                            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            std::cout << "Entrada inválida. Tente novamente. \n";
                            flagInsert = false;
                        }
                        if (flagInsert) {
                            break;
                        }
                    }

                    if (escolha == 0) {
                        goto entrada; // reinicia monitoramento para outro PID
                    }
                    else {
                        break; // sai do loop principal
                    }
                }
                else {
                    std::cout << "O processo de PID: " << medicaoAtual.PID << " será monitorado por mais 5 ciclos... \n";
                    contador = 0; // reseta contador
                }
            }

            // Atualiza medição anterior para calcular deltas no próximo ciclo
            medicaoAnterior = medicaoAtual;
            // Aguarda intervalo definido antes da próxima coleta
            std::this_thread::sleep_for(std::chrono::duration<double>(intervalo));
        }
    }
}

// Define a função 'namespaceAnalyzer', que atuará como um sub-menu para todas as operações de namespace.
void namespaceAnalyzer() {
	int sub; // Variável para armazenar a escolha do sub-menu.
	do { // Inicia um loop 'do-while' que continuará até que o usuário escolha '0' (Voltar).
		std::cout << "\033[1;36m"; // Define a cor do texto para ciano em negrito (código ANSI).
		std::cout << "\n============================================================\n";
		std::cout << "                       Namespace Analyzer                    \n";
		std::cout << "============================================================\n";
		std::cout << "\033[0m"; // reseta as cores pro resto do texto
		std::cout << "\033[1m"; // deixa opções em negrito
		std::cout << " 1. Listar namespaces de um processo\n";
		std::cout << " 2. Comparar namespaces entre dois processos\n";
		std::cout << " 3. Procurar processos em um namespace especifico\n";
		std::cout << " 4. Relatório geral de namespaces\n";
		std::cout << " 0. Voltar ao menu inicial\n";
		std::cout << "------------------------------------------------------------\n";
		std::cout << "Escolha: ";
		std::cout << "\033[0m"; // Reseta as cores para a entrada do usuário.
		std::cin >> sub; // Lê a escolha do usuário e armazena em 'sub'.

		if (std::cin.fail()) {
			std::cin.clear(); // Limpa a entrada
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpa o buffer de entrada
			std::cerr << "Opção Inválida. Por favor, digite apenas números válidos.\n"; // Mostra mensagem ao usuário
			sub = -1;
			continue;
		}

		else if (sub == 1) { // Se o usuário escolheu '1'
			// A função 'escolherPID' mostra uma lista de processos e retorna o PID escolhido pelo usuário
			int pid = escolherPID();
			listNamespaces(pid); // Chama a função para listar os namespaces do PID escolhido.
		}

		else if (sub == 2) { // Se o usuário escolheu '2'
			// A função 'listarProcessos' retorna um vetor ou lista de processos e seus nomes
			while(true) {
				auto processos = listarProcessos();

				// Imprime um cabeçalho para a lista de processos
				std::cout << "\n\033[1;33m=================== Processos disponiveis ==================\033[0m\n";
				// Itera sobre a lista de processos e imprime o PID e o nome de cada um.
				for (const auto& p : processos) {
					std::cout << "PID: " << std::left << std::setw(25) << p.pid << "\tNome: " << p.name << "\n";
				}

				int pid1, pid2; // Variáveis para armazenar os dois PIDs a comparar.
				std::cout << "\nDigite os dois PIDs separados por espaço (ou '0 0' para voltar): ";

				// Lê a linha inteira de uma vez
				std::string linha_input;
				std::getline(std::cin, linha_input);

				// Criar um "stream" a partir dessa linha
				std::stringstream ss(linha_input);

				// Tentar extrair os PIDs e verificar se há lixo no final
				char lixo_extra;

				// Tenta ler pid1 e tenta ler pid2, falha ao tentar ler lixo extra
				if (ss >> pid1 && ss >> pid2 && !(ss >> lixo_extra)) {
					// Checa se o usuário quer voltar
					if (pid1 == 0 && pid2 == 0) {
						break; // Sai do 'while(true)' e volta ao menu principal
					}
					compareNamespaces(pid1, pid2);
				}
				else {
					// Falha se:
					// - O usuário digitou letras (ex: 'a b')
					// - O usuário digitou só um número (ex: '-1')
					// - O usuário digitou mais que dois números (ex: '1 2 3')
					// - O usuário digitou números + lixo (ex: '1 2 abc')
					std::cerr << "Opção Inválida. Por favor, digite apenas dois números válidos.\n";
				}
			}
		}

		else if (sub == 3) { // Se o usuário escolheu '3'
			std::cout << "\n\033[1;33m============== Tipos de Namespaces disponiveis =============\033[0m\n";
			// Define um vetor com os tipos de namespace conhecidos.
			std::vector<std::string> tipos = { "ipc", "mnt", "net", "pid", "user", "uts", "cgroup" };
			// Itera sobre o vetor e imprime cada tipo.
			for (const auto& t : tipos)
				std::cout << "- " << t << "\n";

			std::string tipo; // Variável para armazenar o tipo escolhido.
			std::cout << "\nDigite o tipo de namespace (ex: net): ";
			std::cin >> tipo; // Lê o tipo.

			std::cout << "\n Buscando namespaces do tipo '" << tipo << "'...\n";

			// Esta seção serve para ajudar o usuário, mostrando IDs de namespace válidos.
			// Cria um 'set' para armazenar os IDs únicos encontrados.
			std::set<std::string> idsDisponiveis;
			// Itera por todos os diretórios em /proc
			for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
				std::string pidStr = entry.path().filename(); // Pega o nome do diretório
				// Verifica se o nome é composto apenas por dígitos (é um PID)
				if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

				// Constrói o caminho para o arquivo de namespace (ex: /proc/123/ns/net)
				std::string nsPath = "/proc/" + pidStr + "/ns/" + tipo;
				char buf[256]; // Buffer para ler o link simbólico.
				// Tenta ler o link simbólico que representa o ID do namespace.
				ssize_t len = readlink(nsPath.c_str(), buf, sizeof(buf) - 1);
				if (len != -1) { // Se a leitura for bem-sucedida
					buf[len] = '\0'; // Adiciona o terminador nulo
					idsDisponiveis.insert(std::string(buf)); // Insere o ID no 'set' (ignora duplicatas).
				}
			}

			if (idsDisponiveis.empty()) { // Se nenhum namespace desse tipo foi encontrado
				std::cout << "Nenhum namespace desse tipo foi encontrado.\n";
				continue; // Volta ao início do loop do menu.
			}

			// Imprime os IDs de namespace que foram encontrados
			std::cout << "\n\033[1;33m=============== Namespaces disponiveis (" << tipo << ") ==============\033[0m\n";
			int count = 0; // Contador para limitar a saída
			for (const auto& id : idsDisponiveis) {
				std::cout << " " << ++count << ". " << id << "\n"; // Imprime o ID
				if (count >= 10) { // Limita a 10 para não poluir a tela
					std::cout << "... (mostrando apenas os 10 primeiros)\n";
					break; // Sai do loop de impressão
				}
			}

			std::string idEscolhido; // Variável para o ID alvo.
			// Pede ao usuário o número (ex: 4026531993), não o ID formatado (ex: net:[...]).
			std::cout << "\nDigite o valor exato do namespace (ex: 4026531993): ";
			std::cin >> idEscolhido; // Lê o ID.
			// Chama a função de busca com o tipo e o ID.
			findProcessesInNamespace(tipo, idEscolhido);
		}

		else if (sub == 4) { // Se o usuário escolheu '4'
			gerarRelatorioGeralCompleto(); // Chama a função que gera o relatório completo.
		}

		else if (sub == 0) { // Se o usuário escolheu '0'
			std::cout << "Voltando ao menu principal...\n"; // Imprime mensagem de saída.
		}

		else { // Se a escolha não foi 0, 1, 2, 3 ou 4
			std::cout << "Opção inválida!\n"; // Informa o usuário.
		}

	} while (sub != 0); // O loop continua enquanto 'sub' for diferente de 0.
}

// Esta é a função principal, o ponto de entrada do programa.
int main() {
	int opcao; // Variável para armazenar a escolha do menu principal.
	do { // Inicia o loop do menu principal.
		// Imprime o cabeçalho do menu (com código ANSI para sublinhado e vermelho).
		std::cout << "\n\033[0;4;31m===================== RESOURCE MONITOR =====================\033[0m\n";
		std::cout << "\033[1m"; // Deixa as opções em negrito.
		std::cout << " 1. Gerenciar Cgroups\n";
		std::cout << " 2. Analisar Namespaces\n";
		std::cout << " 3. Perfilador de Recursos\n";
		std::cout << " 4. Executar Experimentos\n";
		std::cout << " 0. Sair\n";
		std::cout << " Escolha: ";
		std::cout << "\033[0m"; // Reseta as cores

		// Tenta ler o 'int'
		if (!(std::cin >> opcao)) { // Se a leitura falhar
			std::cerr << "Opção Inválida. Por favor, digite apenas números válidos.\n";
			std::cin.clear(); // Limpa o "estado de erro" do cin
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Limpa o buffer de entrada
			// Força 'opcao' a ter um valor que não seja 0, para que o 'while(opcao != 0)' não falhe.
			opcao = -1;
			continue; // Pula para a próxima iteração do loop
		}

		switch (opcao) {
		case 1: // Se 'opcao' for 1
			cgroupManager(); // Chama a função do gerenciador de Cgroups.
			break; // Sai do 'switch'.

		case 2: // Se 'opcao' for 2
			namespaceAnalyzer(); // Chama a função do analisador de Namespaces.
			break; // Sai do 'switch'.

		case 3: { // Se 'opcao' for 3
			resourceProfiler(); // Chama a função do perfilador de recursos.
			break; // Sai do 'switch'.
		}

		case 4: { // Se 'opcao' for 4
			executarExperimentos(); // Chama a função que executa os experimentos.
			break; // Sai do 'switch'.
		}

		case 0: // Se 'opcao' for 0
			std::cout << "Encerrando...\n"; // Imprime mensagem de saída.
			break; // Sai do 'switch'.

		default: // Se 'opcao' não for nenhuma das anteriores
			std::cout << "Opção inválida. Tente Novamente\n"; // Informa o usuário.
		}

	} while (opcao != 0); // O loop continua enquanto 'opcao' for diferente de 0.
}
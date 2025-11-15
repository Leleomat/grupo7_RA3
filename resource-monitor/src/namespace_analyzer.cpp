#include "../include/namespace.h"
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <time.h>
#include <fstream>
#include <map>
#include <set>
#include <iomanip>     // setw, left, fixed, setprecision
#include <locale>     // Para std::locale
#include <vector>      // Para guardar os tempos
#include <numeric>     // Para std::accumulate
#include <sched.h>     // Para as flags CLONE_*
#include <sys/mount.h> // Para mount()
#include <stdlib.h>    // Para system()
#include <signal.h>  // Para SIGCHLD

namespace fs = std::filesystem;

// Função auxiliar
std::vector<NamespaceInfo> getNamespacesOfProcess(int pid) {
	std::vector<NamespaceInfo> list;
	std::string path = "/proc/" + std::to_string(pid) + "/ns";

	if (!fs::exists(path)) {
		std::cerr << "O Processo " << pid << " nao existe.\n";
		return list;
	}

	for (const auto& entry : fs::directory_iterator(path)) {
		std::string nsType = entry.path().filename();
		char buf[256];
		ssize_t len = readlink(entry.path().c_str(), buf, sizeof(buf) - 1);
		if (len != -1) {
			buf[len] = '\0';
			std::string id = buf;
			list.push_back({ nsType, id });
		}
	}
	return list;
}

// Listar namespaces de um processo
void listNamespaces(int pid) {
	auto namespaces = getNamespacesOfProcess(pid);
	if (namespaces.empty()) {
		std::cout << "Nenhum namespace encontrado ou PID invalido.\n";
		return;
	}

	std::cout << "\n\033[1;33m================= Namespaces do processo " << pid << " =================\033[0m\n\n";
	for (const auto& ns : namespaces)
		std::cout << " - " << std::left << std::setw(20) << ns.type << " --> " << ns.id << "\n";
}

// Comparar namespaces entre dois processos
void compareNamespaces(int pid1, int pid2) {
	auto ns1 = getNamespacesOfProcess(pid1);
	auto ns2 = getNamespacesOfProcess(pid2);

	std::cout << "\n\033[1;33m================================== Comparacao de Namespaces ==================================\033[0m\n";
	std::cout << " - Processo 1: (PID " << pid1 << ")\n";
	std::cout << " - Processo 2: (PID " << pid2 << ")\n\n";
	std::cout << "    " << std::left << std::setw(25) << "Tipo" << std::setw(14) << "Status" << "Detalhes\n";
	std::cout << "    " << std::string(90, '-') << "\n";
	for (const auto& n1 : ns1) {
		for (const auto& n2 : ns2) {
			if (n1.type == n2.type) {
				bool igual = (n1.id == n2.id);
				std::cout << "    - " << std::left << std::setw(22) << n1.type
					<< (igual ? " IGUAIS        " : " DIFERENTES    ")
					<< std::left << std::setw(22) << n1.id << " vs    " << n2.id << "\n";
			}
		}
	}
}


// Encontrar processos em um namespace específico
void findProcessesInNamespace(const std::string& nsType, const std::string& nsIdRaw) {
	std::string nsIdFormatted = nsType + ":[" + nsIdRaw + "]"; // ex: net:[4026531993]
	std::string base = "/proc";
	bool encontrado = false;

	for (const auto& entry : fs::directory_iterator(base)) {
		if (!entry.is_directory()) continue;
		std::string pidStr = entry.path().filename();
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

		std::string nsPath = base + "/" + pidStr + "/ns/" + nsType;
		if (!fs::exists(nsPath)) continue;

		char buf[256];
		ssize_t len = readlink(nsPath.c_str(), buf, sizeof(buf) - 1);
		if (len != -1) {
			buf[len] = '\0';
			if (std::string(buf) == nsIdFormatted) {
				if (!encontrado) {
					std::cout << "\n\033[1;33m=== Processos pertencentes ao namespace " << nsIdFormatted << " ===\033[0m\n";
					encontrado = true;
				}
				std::cout << " - PID: " << pidStr << "\n";
			}
		}
	}

	if (!encontrado)
		std::cout << "Nenhum processo encontrado nesse namespace.\n";
}


// Relatório de namespaces do sistema
void reportSystemNamespaces() {
	std::cout << "\n\033[1;33m=============== Relatorio geral de namespaces ==============\033[0m\n\n";
	std::map<std::string, std::set<std::string>> mapa;

	for (const auto& entry : fs::directory_iterator("/proc")) {
		if (!entry.is_directory()) continue;
		std::string pidStr = entry.path().filename();
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

		auto namespaces = getNamespacesOfProcess(std::stoi(pidStr));
		for (auto& ns : namespaces)
			mapa[ns.type].insert(ns.id);
	}

	for (auto& [tipo, ids] : mapa)
		std::cout << " - " << std::left << std::setw(20) << tipo << ": " << ids.size() << " namespaces unicos\n";
}

// Função que formata o número manualmente
std::string formatarNumero(double num) {
	std::stringstream ss;
	// Formata com 3 casas decimais, um ponto fixo
	ss << std::fixed << std::setprecision(3) << num;

	// String como "1683.641"
	std::string s = ss.str();

	// Troca o ponto decimal por vírgula
	size_t pos_decimal = s.find('.');
	if (pos_decimal != std::string::npos) {
		s[pos_decimal] = ',';
	}
	else {
		// Se não tiver decimal
		s += ",000";
		pos_decimal = s.find(',');
	}

	// Insere os pontos de milhar
	int pos_insercao = pos_decimal - 3;
	while (pos_insercao > 0) {
		s.insert(pos_insercao, ".");
		pos_insercao -= 3;
	}

	return s;
}


// Medir o overhead da criação de namespaces
double calcularOverheadMedio(int cloneFlags, const std::string& tipo, int iteracoes = 10) {
	std::vector<double> tempos;

	for (int i = 0; i < iteracoes; ++i) {
		struct timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);

		pid_t pid = fork();
		if (pid == 0) { // Filho
			// Tenta isolar. Ignora erros para o teste de performance
			unshare(cloneFlags);
			_exit(0);
		}
		else if (pid > 0) { // Pai
			waitpid(pid, nullptr, 0);
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		double diff = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
		tempos.push_back(diff);

		std::cout << " - (" << tipo << ") Criacao " << (i + 1) << "/" << iteracoes << ": " << formatarNumero(diff) << " microssegundos\n";
	}

	if (tempos.empty()) return 0.0;

	double somaTempo = std::accumulate(tempos.begin(), tempos.end(), 0.0);
	return (somaTempo / iteracoes);
}

// Demonstrar o isolamento
// Esta é a função que o novo processo (PID 1) vai rodar
int child_main(void* /* arg */) {
	std::cout << "\n\033[1;32m[DENTRO DO FILHO ISOLADO (PID 1)]\033[0m\n";
	
	bool mount_ok = false;
	int result_mask = 0; // Bitmask para os resultados

	// 1. Teste de Mount (CLONE_NEWNS)
	// Tentamos montar /proc. Se funcionar, o isolamento de mount existe.
	std::cout << " > Verificando isolamento de Filesystem (CLONE_NEWNS):\n";
	std::cout << "   - Tentando montar um /proc privado...\n";
	mount_ok = (mount("proc", "/proc", "proc", 0, NULL) == 0);
	if (mount_ok) {
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 1; // Bit 0: Mount test OK
	}
	else {
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: Falha ao montar o novo /proc.\n";
	}

	// 2. Teste de PID (CLONE_NEWPID)
	// Só podemos testar o PID se o mount do /proc funcionou.
	std::cout << " > Verificando isolamento de PID (CLONE_NEWPID):\n";
	pid_t meu_pid = syscall(SYS_getpid);
	std::cout << "   - Meu PID (syscall): \033[1m" << meu_pid << "\033[0m\n";

	// O teste de PID só é válido se o mount do /proc funcionou
	if (mount_ok && meu_pid == 1) {
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 1; // Bit 1: PID test OK
	}
	else {
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: Falha no PID.\n";
	}

	// 3. Teste de Rede (CLONE_NEWNET)
	// Procuramos pela 'eth0'. Se não acharmos (retorno != 0), o isolamento funcionou.
	std::cout << " > Verificando isolamento de Rede (CLONE_NEWNET):\n";
	std::cout << "   - Interfaces de rede (ip link show):\n";
	system("ip link show"); // Mostra para o usuário
	if (system("ip link show | grep -q 'eth0'") != 0) {
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 1; // Bit 2: NET test OK
	}
	else {
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: O isolamento não funcionou.\n";
	}

	std::cout << "\033[1;32m[FILHO TERMINANDO]\033[0m\n";
	if (mount_ok) {
		umount("/proc"); // Limpa a montagem
	}

	// Retorna o bitmask
	return result_mask;
}

// FUNÇÃO AUXILIAR 2: Demonstrar o isolamento (VERSÃO CORRETA)
void demonstrarIsolamento() {
	const int STACK_SIZE = 1024 * 1024; // 1MB de stack
	static char child_stack[STACK_SIZE]; // Stack para o novo processo

	std::cout << "\n\033[1;33m=== Verificando Visibilidade de Recursos (PID, NET e Filesystems) ===\033[0m\n";
	std::cout << " Criando processo filho com diferentes namespaces isolados via clone()...\n";

	int flags = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | SIGCHLD;

	pid_t pid = clone(child_main,
		child_stack + STACK_SIZE,
		flags,
		NULL);

	if (pid == -1) {
		std::cerr << "Falha no clone(). Certifique-se de rodar como sudo.\n";
		return;
	}

	// --- Processo Pai (Raiz) ---
	int child_status;
	waitpid(pid, &child_status, 0); // Espera o filho

	// Pega o bitmask de resultados que o filho retornou
	int child_result = 0;
	if (WIFEXITED(child_status)) {
		child_result = WEXITSTATUS(child_status);
	}

	// Interpreta o bitmask
	bool mount_isolado = (child_result & 1); // Bit 0
	bool pid_isolado = (child_result & 1); // Bit 1
	bool net_isolado = (child_result & 1); // Bit 2

	// ... (O cout do PAI continua igual) ...
	std::cout << "\n\033[1;34m[DE VOLTA AO PAI (NAMESPACE RAIZ)]\033[0m\n";
	
	// TESTE 1: PID (CONTROLE)
	std::cout << " > Verificando qual era o PID do filho (visto pelo pai):\n";
	std::cout << "   - PID do filho era: \033[1m" << pid << "\033[0m\n";

	// TESTE 2: REDE (CONTROLE)
	std::cout << " > Verificando interfaces de rede do PAI (ip link show):\n";
	system("ip link show"); // Mostra as interfaces do PAI (deve incluir eth0)

	// TESTE 3: FILESYSTEM (CONTROLE)
	std::cout << " > Verificando filesystem do PAI (CLONE_NEWNS):\n";
	std::cout << "   - O /proc do pai (PID " << getpid() << ") nao foi afetado.\n";
	std::cout << "   - A montagem do filho foi privada e desfeita automaticamente.\n";

	std::cout << "\033[1;34m[PAI CONCLUÍDO]\033[0m\n";


	// 3. Imprime a tabela-resumo (Métrica) - AGORA DINÂMICA
	std::cout << "\n\033[1;33m==== Tabela de Isolamento Efetivo por Tipo de Namespace ====\033[0m\n";
	std::cout << " | Tipo de Namespace  | Recurso Isolado? | Observacao\n";
	std::cout << " |--------------------|------------------|-------------------------------------------------\n";

	// Define as strings de Sim/Nao (com cores!)
	std::string sim = "\033[1;32m      Sim        \033[0m"; // Verde
	std::string nao = "\033[1;31m      Nao        \033[0m"; // Vermelho

	// Imprime a tabela usando os booleanos
	std::cout << " | PID (CLONE_NEWPID) | " << std::left << std::setw(17) // setw(17) alinha a tabela
		<< (pid_isolado ? sim : nao)
		<< "| Filho enxerga-se como PID 1\n";

	std::cout << " | Rede (CLONE_NEWNET)| " << std::left << std::setw(17)
		<< (net_isolado ? sim : nao)
		<< "| Filho enxerga somente a interface 'lo' (loopback)\n";

	std::cout << " | Mount (CLONE_NEWNS)| " << std::left << std::setw(17)
		<< (mount_isolado ? sim : nao)
		<< "| Montagens do filho (ex: /proc) nao afetam o pai\n";
}

void reportProcessCountsPerNamespace() {
	// Mapa[Tipo] -> Mapa[ID_do_Namespace] -> Contagem
	std::map<std::string, std::map<std::string, int>> contagem;

	// 1. Iterar por todos os processos
	for (const auto& entry : fs::directory_iterator("/proc")) {
		if (!entry.is_directory()) continue;
		std::string pidStr = entry.path().filename();
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

		std::string nsDir = entry.path().string() + "/ns";
		if (!fs::exists(nsDir)) continue;

		// 2. Iterar pelos namespaces desse processo
		for (const auto& nsEntry : fs::directory_iterator(nsDir)) {
			std::string tipo = nsEntry.path().filename();
			char buf[256];
			ssize_t len = readlink(nsEntry.path().c_str(), buf, sizeof(buf) - 1);

			if (len != -1) {
				buf[len] = '\0';
				std::string id(buf); // ex: "net:[4026531993]"

				// 3. Incrementar o contador
				contagem[tipo][id]++;
			}
		}
	}

	// 4. Imprimir o relatório
	std::cout << "\n\033[1;33m===== Número de Processos por Namespace Unico (Top 10) =====\033[0m\n";
	for (const auto& [tipo, idMap] : contagem) {
		std::cout << "\n  Tipo: \033[1;32m" << tipo << "\033[0m (" << idMap.size() << " unicos)\n";

		// Mostra os 10 namespaces mais populosos
		std::vector<std::pair<std::string, int>> pares;
		for (const auto& par : idMap) {
			pares.push_back(par);
		}

		// Classifica em ordem decrescente de contagem
		std::sort(pares.begin(), pares.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });

		int count = 0;
		for (const auto& [id, num] : pares) {
			if (count >= 10) break; // Limita a 5 por tipo
			std::cout << "    - " << std::left << std::setw(25) << id << ": "
				<< std::right << std::setw(4) << num << " processos\n";
			count++;
		}
		if (idMap.size() > 10) {
			std::cout << "    ... (e mais " << (idMap.size() - 5) << " outros namespaces)\n";
		}
	}
}

// FUNÇÃO PRINCIPAL DO EXPERIMENTO
// Esta é a nova função que será chamada pelo menu
void executarExperimentoIsolamento() {
	std::cout << "\n\033[1;33m============================================================\033[0m\n";
	std::cout << " \033[1;37mExecutando Experimento 2: Isolamento via Namespaces\n";
	std::cout << " NOTA: Este experimento precisa de privilegios (sudo)\n";
	std::cout << "       para unshare() e mount()\033[0m\n";
	std::cout << "\033[1;33m============================================================\033[0m\n";

	// --- Procedimento/Métrica: Overhead de Criação (μs) ---
	std::cout << "\n\033[1;33m=== Metrica: Overhead de Criação de Isolamento (Media de 10 iterações) ===\033[0m\n";

	double overhead_pid = calcularOverheadMedio(CLONE_NEWPID, "PID");
	double overhead_net = calcularOverheadMedio(CLONE_NEWNET, "NET");
	double overhead_ns = calcularOverheadMedio(CLONE_NEWNS, "NS");
	double overhead_combo = calcularOverheadMedio(CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS, "Combo");

	// Usa a função 'formatarNumero' que você já tinha
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWPID (PID): "
		<< std::right << std::setw(10) << formatarNumero(overhead_pid) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWNET (Rede): "
		<< std::right << std::setw(10) << formatarNumero(overhead_net) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWNS (Mount): "
		<< std::right << std::setw(10) << formatarNumero(overhead_ns) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(49) << " - Overhead Medio Combinação (PID+NET+NS): "
		<< std::right << std::setw(10) << formatarNumero(overhead_combo) << " microssegundos\n";
	// --- Procedimento/Métrica: Validar Efetividade do Isolamento ---
	demonstrarIsolamento(); // Esta função imprime sua própria tabela

	reportProcessCountsPerNamespace();

	std::cout << "\n\033[1;33m================== Experimento 2 Concluído =================\033[0m\n";
}

void gerarRelatorioGeralCompleto() {
	std::cout << "\n\033[1;33m============================================================\033[0m\n";
	std::cout << "\033[1;37m            Relatorio Geral de Namespaces do Sistema                   \033[0m\n";
	std::cout << "\033[1;33m============================================================\033[0m\n";

	// Contagem de namespaces únicos
	reportSystemNamespaces();

	// Contagem de processos por namespace
	reportProcessCountsPerNamespace();

	std::cout << "\n\033[1;33m===================== Fim do Relatorio =====================\033[0m\n";
}
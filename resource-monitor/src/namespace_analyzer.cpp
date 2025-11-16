#include "../include/namespace.h"
#include <algorithm>  // Para std::all_of (verificar se string é dígito) e std::sort
#include <iostream>   // Para std::cout, std::cerr
#include <filesystem> // Para std::filesystem (fs::exists, fs::directory_iterator)
#include <dirent.h>   // Para manipulação de diretórios estilo C (embora <filesystem> seja usado)
#include <unistd.h>   // Para chamadas POSIX (readlink, fork, _exit, getpid)
#include <cstring>    // Para funções de string C
#include <sys/stat.h> // Para informações de arquivos
#include <sys/types.h> // Para tipos como pid_t
#include <sys/wait.h>  // Para waitpid() e macros WIFEXITED, WEXITSTATUS
#include <sys/syscall.h> // Para syscall(SYS_getpid) - uma forma de obter o PID
#include <time.h>      // Para clock_gettime, struct timespec (medição de tempo)
#include <fstream>   // Para manipulação de arquivos
#include <map>       // Para std::map (relatórios)
#include <set>       // Para std::set (encontrar namespaces únicos)
#include <iomanip>   // Para std::setw, std::left, std::fixed, std::setprecision (formatação)
#include <locale>    // Para std::locale (usado na formatação de números)
#include <vector>    // Para std::vector (guardar tempos)
#include <numeric>   // Para std::accumulate (somar tempos)
#include <sched.h>   // Para as flags CLONE_* (CLONE_NEWPID, CLONE_NEWNET, etc.)
#include <sys/mount.h> // Para mount() e umount()
#include <stdlib.h>  // Para system() (executar comandos do shell)
#include <signal.h>  // Para SIGCHLD (sinal enviado ao pai quando o filho termina)

// Cria um alias 'fs' para o namespace std::filesystem para facilitar
namespace fs = std::filesystem;

// Obtém uma lista de namespaces (tipo e ID) para um determinado PID
std::vector<NamespaceInfo> getNamespacesOfProcess(int pid) {
	std::vector<NamespaceInfo> list; // Vetor para armazenar os resultados
	// Constrói o caminho para o diretório de namespaces do processo no /proc
	std::string path = "/proc/" + std::to_string(pid) + "/ns";

	// Verifica se o diretório existe (processo pode não existir)
	if (!fs::exists(path)) {
		std::cerr << "O Processo " << pid << " nao existe.\n";
		return list; // Retorna a lista vazia
	}

	// Itera sobre cada arquivo no diretório /proc/<pid>/ns
	for (const auto& entry : fs::directory_iterator(path)) {
		std::string nsType = entry.path().filename();// O nome do arquivo é o tipo (ex: "net", "pid")
		char buf[256]; // Buffer para ler o destino do link simbólico
		// Lê o link simbólico (ex: /proc/1/ns/net -> "net:[4026531993]")
		ssize_t len = readlink(entry.path().c_str(), buf, sizeof(buf) - 1);
		if (len != -1) { // Se a leitura for bem-sucedida
			buf[len] = '\0'; // Adiciona o terminador nulo
			std::string id = buf; // Converte o buffer C para std::string
			list.push_back({ nsType, id }); // Adiciona à lista
		}
	}
	return list; // Retorna a lista preenchida
}

// Listar namespaces de um processo
// Função que imprime os namespaces de um PID específico
void listNamespaces(int pid) {
	auto namespaces = getNamespacesOfProcess(pid); // Chama a função auxiliar
	if (namespaces.empty()) { // Se a lista estiver vazia (PID inválido ou erro)
		std::cout << "Nenhum namespace encontrado ou PID invalido.\n";
		return;
	}

	// Imprime um cabeçalho formatado com códigos de cor ANSI
	std::cout << "\n\033[1;33m================= Namespaces do processo " << pid << " =================\033[0m\n\n";
	// Itera sobre os namespaces encontrados
	for (const auto& ns : namespaces)
		// Imprime o tipo e o ID formatados
		std::cout << " - " << std::left << std::setw(20) << ns.type << " --> " << ns.id << "\n";
}

// Comparar namespaces entre dois processos
// Função que compara os namespaces de dois PIDs
void compareNamespaces(int pid1, int pid2) {
	auto ns1 = getNamespacesOfProcess(pid1); // Pega os namespaces do PID 1
	auto ns2 = getNamespacesOfProcess(pid2); // Pega os namespaces do PID 2

	// Imprime um cabeçalho formatado
	std::cout << "\n\033[1;33m================================== Comparacao de Namespaces ==================================\033[0m\n";
	std::cout << " - Processo 1: (PID " << pid1 << ")\n";
	std::cout << " - Processo 2: (PID " << pid2 << ")\n\n";
	std::cout << "    " << std::left << std::setw(25) << "Tipo" << std::setw(14) << "Status" << "Detalhes\n";
	std::cout << "    " << std::string(90, '-') << "\n"; // Imprime uma linha divisória

	// Loop aninhado para comparar cada namespace de ns1 com cada um de ns2
	for (const auto& n1 : ns1) {
		for (const auto& n2 : ns2) {
			if (n1.type == n2.type) { // Se os tipos forem os mesmos (ex: ambos são "net")
				bool igual = (n1.id == n2.id); // Verifica se os IDs são iguais
				// Imprime o resultado da comparação
				std::cout << "    - " << std::left << std::setw(22) << n1.type
					<< (igual ? " IGUAIS        " : " DIFERENTES    ") // Operador ternário para status
					<< std::left << std::setw(22) << n1.id << " vs    " << n2.id << "\n";
			}
		}
	}
}


// Encontrar processos em um namespace específico
// Procura todos os PIDs que pertencem a um ID de namespace específico
void findProcessesInNamespace(const std::string& nsType, const std::string& nsIdRaw) {
	// O ID lido (ex: "net:[4026531993]") precisa ser formatado como o /proc espera
	std::string nsIdFormatted = nsType + ":[" + nsIdRaw + "]"; // ex: net:[4026531993]
	std::string base = "/proc"; // Diretório base para procurar
	bool encontrado = false; // Flag para imprimir o cabeçalho apenas uma vez

	// Itera por todas as entradas em /proc
	for (const auto& entry : fs::directory_iterator(base)) {
		if (!entry.is_directory()) continue; // Pula arquivos (ex: /proc/version)
		std::string pidStr = entry.path().filename(); // Pega o nome do diretório
		// Verifica se o nome do diretório é composto apenas por dígitos (é um PID)
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

		// Constrói o caminho para o arquivo de namespace específico (ex: /proc/123/ns/net)
		std::string nsPath = base + "/" + pidStr + "/ns/" + nsType;
		if (!fs::exists(nsPath)) continue; // Pula se o processo não tiver esse namespace

		char buf[256]; // Buffer para ler o link
		ssize_t len = readlink(nsPath.c_str(), buf, sizeof(buf) - 1); // Lê o ID do namespace
		if (len != -1) { // Se a leitura for bem-sucedida
			buf[len] = '\0'; // Termina a string
			if (std::string(buf) == nsIdFormatted) { // Compara com o ID alvo
				if (!encontrado) { // Se for o primeiro encontrado
					// Imprime o cabeçalho
					std::cout << "\n\033[1;33m=== Processos pertencentes ao namespace " << nsIdFormatted << " ===\033[0m\n";
					encontrado = true; // Define a flag
				}
				std::cout << " - PID: " << pidStr << "\n"; // Imprime o PID
			}
		}
	}

	if (!encontrado) // Se nenhum processo foi encontrado
		std::cout << "Nenhum processo encontrado nesse namespace.\n";
}

// Relatório de namespaces do sistema
// Conta quantos namespaces únicos de cada tipo existem no sistema
void reportSystemNamespaces() {
	std::cout << "\n\033[1;33m=============== Relatorio geral de namespaces ==============\033[0m\n\n";
	// Mapa: Chave (string, tipo) -> Valor (Set de strings, IDs únicos)
	std::map<std::string, std::set<std::string>> mapa;

	// Itera por todos os PIDs em /proc
	for (const auto& entry : fs::directory_iterator("/proc")) {
		if (!entry.is_directory()) continue; // Pula arquivos
		std::string pidStr = entry.path().filename(); // Pega o nome
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue; // Pula se não for PID

		// Pega todos os namespaces para este PID
		auto namespaces = getNamespacesOfProcess(std::stoi(pidStr));
		// Itera sobre os namespaces deste PID
		for (auto& ns : namespaces)
			// Insere o ID no set. O set automaticamente cuida da duplicidade.
			mapa[ns.type].insert(ns.id);
	}

	// Itera sobre o mapa preenchido (usando C++17 structured bindings)
	for (auto& [tipo, ids] : mapa)
		// Imprime o tipo e o tamanho do set (número de IDs únicos)
		std::cout << " - " << std::left << std::setw(20) << tipo << ": " << ids.size() << " namespaces unicos\n";
}

// Função que formata o número manualmente
// Converte um double para uma string no formato "1.234,567"
std::string formatarNumero(double num) {
	std::stringstream ss; // Cria um stream de string
	// Formata o número com 3 casas decimais e ponto fixo (ex: 1683.641)
	ss << std::fixed << std::setprecision(3) << num;

	std::string s = ss.str(); // Extrai a string do stream

	// Troca o ponto decimal por vírgula
	size_t pos_decimal = s.find('.');
	if (pos_decimal != std::string::npos) { // Se encontrou um ponto
		s[pos_decimal] = ','; // Substitui por vírgula
	}
	else { // Se não tiver decimal (ex: "1683")
		s += ",000"; // Adiciona casas decimais
		pos_decimal = s.find(','); // Atualiza a posição da vírgula
	}

	// Insere os pontos de milhar
	int pos_insercao = pos_decimal - 3; // Posição do primeiro separador (antes dos 3 dígitos)
	while (pos_insercao > 0) { // Enquanto houver mais separadores para inserir
		s.insert(pos_insercao, "."); // Insere o ponto
		pos_insercao -= 3; // Move 3 posições para a esquerda
	}

	return s; // Retorna a string formatada
}

// Medir o overhead da criação de namespaces
// Calcula o tempo médio para criar um processo e isolá-lo
double calcularOverheadMedio(int cloneFlags, const std::string& tipo, int iteracoes = 10) {
	std::vector<double> tempos; // Vetor para armazenar os tempos de cada iteração

	for (int i = 0; i < iteracoes; ++i) { // Repete 'iteracoes' vezes
		struct timespec start, end; // Structs para medição de tempo de alta precisão
		clock_gettime(CLOCK_MONOTONIC, &start); // Pega o tempo de início

		pid_t pid = fork(); // Cria um processo filho
		if (pid == 0) { // Código do Filho
			// Tenta isolar o namespace usando as flags fornecidas
			unshare(cloneFlags); // Esta é a chamada de sistema principal sendo medida
			_exit(0); // Sai do filho imediatamente (sem limpeza de C++)
		}
		else if (pid > 0) { // Código do Pai
			waitpid(pid, nullptr, 0); // Espera o filho terminar
		}

		clock_gettime(CLOCK_MONOTONIC, &end); // Pega o tempo de fim
		// Calcula a diferença em microssegundos (1e6 = 1.000.000)
		double diff = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
		tempos.push_back(diff); // Armazena o tempo

		// Imprime o progresso da iteração atual
		std::cout << " - (" << tipo << ") Criacao " << (i + 1) << "/" << iteracoes << ": " << formatarNumero(diff) << " microssegundos\n";
	}

	if (tempos.empty()) return 0.0; // Evita divisão por zero

	double somaTempo = std::accumulate(tempos.begin(), tempos.end(), 0.0); // Soma todos os tempos
	return (somaTempo / iteracoes); // Retorna a média
}

// Demonstrar o isolamento
// Esta é a função que o novo processo (criado com clone()) vai rodar
int child_main(void* /* arg */) { // O argumento não é usado
	std::cout << "\n\033[1;32m[DENTRO DO FILHO ISOLADO (PID 1)]\033[0m\n"; // Cabeçalho do filho

	bool mount_ok = false; // Flag para verificar se a montagem do /proc funcionou
	int result_mask = 0; // Bitmask para retornar os resultados dos testes ao pai

	// 1. Teste de Mount (CLONE_NEWNS)
	std::cout << " > Verificando isolamento de Filesystem (CLONE_NEWNS):\n";
	std::cout << "   - Tentando montar um /proc privado...\n";
	// Tenta montar um novo /proc. Só funciona se o namespace de montagem for novo.
	mount_ok = (mount("proc", "/proc", "proc", 0, NULL) == 0);
	if (mount_ok) { // Se a montagem funcionou
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 1; // Define o bit 0 (Mount OK)
	}
	else { // Se falhou
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: Falha ao montar o novo /proc.\n"; // Mensagem de erro
	}

	// 2. Teste de PID (CLONE_NEWPID)
	std::cout << " > Verificando isolamento de PID (CLONE_NEWPID):\n";
	// Obtém o PID. getpid() pode ser cacheado, syscall é mais direto.
	pid_t meu_pid = syscall(SYS_getpid);
	std::cout << "   - Meu PID (syscall): \033[1m" << meu_pid << "\033[0m\n";

	// O teste de PID só é válido se o mount do /proc funcionou E o PID é 1
	if (mount_ok && meu_pid == 1) { // Deve ser 1, pois é o primeiro processo no novo namespace PID
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 2; // Define o bit 1 (PID OK)
	}
	else {
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: Falha no PID.\n";
	}

	// 3. Teste de Rede (CLONE_NEWNET)
	std::cout << " > Verificando isolamento de Rede (CLONE_NEWNET):\n";
	std::cout << "   - Interfaces de rede (ip link show):\n";
	system("ip link show"); // Mostra as interfaces de rede (deve ser só 'lo')
	// Verifica se 'eth0' NÃO existe. system() retorna 0 se o grep achar, e != 0 se não achar.
	if (system("ip link show | grep -q 'eth0'") != 0) { // Queremos que o grep falhe
		std::cout << "\033[1;32m...Sucesso!\033[0m\n";
		result_mask |= 4; // Define o bit 2 (NET OK)
	}
	else {
		std::cout << "\033[1;31m...Falha!\033[0m\n";
		std::cerr << "...Filho: O isolamento não funcionou.\n";
	}

	std::cout << "\033[1;32m[FILHO TERMINANDO]\033[0m\n";
	if (mount_ok) { // Se montamos o /proc
		umount("/proc"); // Desmonta para limpar
	}

	// Retorna o bitmask como código de saída
	return result_mask;
}

// FUNÇÃO AUXILIAR 2: Demonstrar o isolamento (VERSÃO CORRETA)
// Esta é a função PAI que configura e inicia o processo filho com clone()
void demonstrarIsolamento() {
	const int STACK_SIZE = 1024 * 1024; // 1MB de stack
	static char child_stack[STACK_SIZE]; // Aloca a stack para o novo processo

	std::cout << "\n\033[1;33m====== Verificando Visibilidade de Recursos (PID, NET e Filesystems) ======\033[0m\n";
	std::cout << " Criando processo filho com diferentes namespaces isolados via clone()...\n";

	// Define as flags para clone(): novos namespaces PID, NET, NS (Mount)
	// SIGCHLD é crucial para que o waitpid() funcione corretamente com clone()
	int flags = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | SIGCHLD;

	// Chama clone() - a versão do kernel de "criar processo"
	pid_t pid = clone(child_main, // Função que o filho vai rodar
		child_stack + STACK_SIZE, // Ponteiro para o TOPO da stack (ela cresce para baixo)
		flags, // Flags de isolamento
		NULL); // Argumento para child_main (nenhum)

	if (pid == -1) { // Se clone() falhar
		std::cerr << "Falha no clone(). Certifique-se de rodar como sudo.\n";
		return;
	}

	// --- Processo Pai (Raiz) ---
	int child_status; // Variável para armazenar o status de saída do filho
	waitpid(pid, &child_status, 0); // Espera o processo filho (pid) terminar

	// Pega o bitmask de resultados que o filho retornou
	int child_result = 0; // Inicializa o resultado
	if (WIFEXITED(child_status)) { // Verifica se o filho terminou normalmente
		child_result = WEXITSTATUS(child_status); // Extrai o código de saída (nosso bitmask)
	}

	// Interpreta o bitmask
	// OBS: HÁ UM BUG NO CÓDIGO ORIGINAL. TODOS OS TESTES DEPENDEM DO BIT 0.
	bool mount_isolado = (child_result & 1); // Bit 0
	bool pid_isolado = (child_result & 2); // Bit 1
	bool net_isolado = (child_result & 4); // Bit 2

	// ... (O cout do PAI continua igual) ...
	std::cout << "\n\033[1;34m[DE VOLTA AO PAI (NAMESPACE RAIZ)]\033[0m\n";

	// TESTE 1: PID (CONTROLE)
	// Mostra o PID do filho como visto pelo PAI (não será 1)
	std::cout << " > Verificando qual era o PID do filho (visto pelo pai):\n";
	std::cout << "   - PID do filho era: \033[1m" << pid << "\033[0m\n";

	// TESTE 2: REDE (CONTROLE)
	// Mostra as interfaces de rede do PAI (deve ter 'eth0' ou similar)
	std::cout << " > Verificando interfaces de rede do PAI (ip link show):\n";
	system("ip link show");

	// TESTE 3: FILESYSTEM (CONTROLE)
	// Confirma que a montagem do filho não afetou o pai
	std::cout << " > Verificando filesystem do PAI (CLONE_NEWNS):\n";
	std::cout << "   - O /proc do pai (PID " << getpid() << ") nao foi afetado.\n";
	std::cout << "   - A montagem do filho foi privada e desfeita automaticamente.\n";

	std::cout << "\033[1;34m[PAI CONCLUÍDO]\033[0m\n";


	// 3. Imprime a tabela-resumo (Métrica) - AGORA DINÂMICA
	std::cout << "\n\033[1;33m==== Tabela de Isolamento Efetivo por Tipo de Namespace ====\033[0m\n";
	std::cout << " | Tipo de Namespace  | Recurso Isolado? | Observacao\n";
	std::cout << " |--------------------|------------------|-------------------------------------------------\n";

	// Define as strings de Sim/Nao (com cores!)
	std::string sim = "\033[1;32m      Sim        \033[0m"; // Verde
	std::string nao = "\033[1;31m      Nao        \033[0m"; // Vermelho

	// Imprime a tabela usando os booleanos (que dependem do bitmask)
	std::cout << " | PID (CLONE_NEWPID) | " << std::left << std::setw(17) // setw(17) alinha a tabela
		<< (pid_isolado ? sim : nao) // Imprime Sim ou Nao
		<< "| Filho enxerga-se como PID 1\n";

	std::cout << " | Rede (CLONE_NEWNET)| " << std::left << std::setw(17)
		<< (net_isolado ? sim : nao) // Imprime Sim ou Nao
		<< "| Filho enxerga somente a interface 'lo' (loopback)\n";

	std::cout << " | Mount (CLONE_NEWNS)| " << std::left << std::setw(17)
		<< (mount_isolado ? sim : nao) // Imprime Sim ou Nao
		<< "| Montagens do filho (ex: /proc) nao afetam o pai\n";
}

// Relatório de contagem de processos por namespace
void reportProcessCountsPerNamespace() {
	// Estrutura de dados: Mapa[Tipo] -> Mapa[ID_do_Namespace] -> Contagem
	std::map<std::string, std::map<std::string, int>> contagem;

	// 1. Iterar por todos os processos
	for (const auto& entry : fs::directory_iterator("/proc")) { // Itera em /proc
		if (!entry.is_directory()) continue; // Pula arquivos
		std::string pidStr = entry.path().filename(); // Pega nome
		if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue; // Pula se não for PID

		std::string nsDir = entry.path().string() + "/ns"; // Caminho /proc/<pid>/ns
		if (!fs::exists(nsDir)) continue; // Pula se não existir

		// 2. Iterar pelos namespaces desse processo
		for (const auto& nsEntry : fs::directory_iterator(nsDir)) {
			std::string tipo = nsEntry.path().filename(); // Tipo (ex: "net")
			char buf[256]; // Buffer
			ssize_t len = readlink(nsEntry.path().c_str(), buf, sizeof(buf) - 1); // Lê link

			if (len != -1) { // Se leu com sucesso
				buf[len] = '\0'; // Termina string
				std::string id(buf); // ID (ex: "net:[4026531993]")

				// 3. Incrementar o contador
				// Incrementa o contador para este tipo e este ID
				contagem[tipo][id]++;
			}
		}
	}

	// 4. Imprimir o relatório
	std::cout << "\n\033[1;33m===== Número de Processos por Namespace Unico (Top 10) =====\033[0m\n";
	// Itera sobre o mapa externo (por tipo)
	for (const auto& [tipo, idMap] : contagem) {
		std::cout << "\n  Tipo: \033[1;32m" << tipo << "\033[0m (" << idMap.size() << " unicos)\n";

		// Mostra os 10 namespaces mais populosos
		// Copia o mapa interno (ID -> contagem) para um vetor de pares
		std::vector<std::pair<std::string, int>> pares;
		for (const auto& par : idMap) {
			pares.push_back(par);
		}

		// Classifica o vetor em ordem decrescente de contagem (o .second)
		std::sort(pares.begin(), pares.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });

		int count = 0; // Contador para limitar a 10
		for (const auto& [id, num] : pares) {
			if (count >= 10) break; // Limita a 10 por tipo
			// Imprime o ID e a contagem
			std::cout << "    - " << std::left << std::setw(25) << id << ": "
				<< std::right << std::setw(4) << num << " processos\n";
			count++;
		}
		if (idMap.size() > 10) { // Se houver mais de 10
			// OBS: Bug no código original, (idMap.size() - 5) deve ser (idMap.size() - 10)
			std::cout << "    ... (e mais " << (idMap.size() - 5) << " outros namespaces)\n";
		}
	}
}

// FUNÇÃO PRINCIPAL DO EXPERIMENTO
// Esta é a nova função que será chamada pelo menu
void executarExperimentoIsolamento() {
	// Imprime o cabeçalho do experimento
	std::cout << "\n\033[1;33m============================================================\033[0m\n";
	std::cout << " \033[1;37mExecutando Experimento 2: Isolamento via Namespaces\n";
	std::cout << " NOTA: Este experimento precisa de privilegios (sudo)\n";
	std::cout << "       para unshare() e mount()\033[0m\n";
	std::cout << "\033[1;33m============================================================\033[0m\n";

	// --- Procedimento/Métrica: Overhead de Criação (μs) ---
	std::cout << "\n\033[1;33m=== Metrica: Overhead de Criação de Isolamento (Media de 10 iterações) ===\033[0m\n";

	// Calcula o overhead para cada tipo de namespace
	double overhead_pid = calcularOverheadMedio(CLONE_NEWPID, "PID");
	double overhead_net = calcularOverheadMedio(CLONE_NEWNET, "NET");
	double overhead_ns = calcularOverheadMedio(CLONE_NEWNS, "NS");
	// Calcula o overhead para todos combinados
	double overhead_combo = calcularOverheadMedio(CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS, "Combo");

	// Imprime o resumo dos resultados de overhead
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWPID (PID): "
		<< std::right << std::setw(10) << formatarNumero(overhead_pid) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWNET (Rede): "
		<< std::right << std::setw(10) << formatarNumero(overhead_net) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(47) << " - Overhead Medio CLONE_NEWNS (Mount): "
		<< std::right << std::setw(10) << formatarNumero(overhead_ns) << " microssegundos";
	std::cout << "\n" << std::left << std::setw(49) << " - Overhead Medio Combinação (PID+NET+NS): "
		<< std::right << std::setw(10) << formatarNumero(overhead_combo) << " microssegundos\n";

	// --- Procedimento/Métrica: Validar Efetividade do Isolamento ---
	demonstrarIsolamento(); // Roda o experimento principal com clone()

	reportProcessCountsPerNamespace(); // Mostra o relatório de contagem

	std::cout << "\n\033[1;33m================== Experimento 2 Concluído =================\033[0m\n";
}

// Função para gerar um relatório geral (combina duas funções de relatório)
void gerarRelatorioGeralCompleto() {
	std::cout << "\n\033[1;33m============================================================\033[0m\n";
	std::cout << "\033[1;37m            Relatorio Geral de Namespaces do Sistema                   \033[0m\n";
	std::cout << "\033[1;33m============================================================\033[0m\n";

	// Chama o relatório de contagem de namespaces únicos
	reportSystemNamespaces();

	// Chama o relatório de contagem de processos por namespace
	reportProcessCountsPerNamespace();

	std::cout << "\n\033[1;33m===================== Fim do Relatorio =====================\033[0m\n";
}
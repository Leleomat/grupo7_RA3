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
#include <time.h>
#include <fstream>
#include <map>
#include <set>
#include <iomanip>     // setw, left, fixed, setprecision
#include <locale>     // Para std::locale
#include <vector>      // Para guardar os tempos
#include <numeric>     // Para std::accumulate

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

	std::cout << "\n\033[1;33m================= Namespaces do processo " << pid << " =================\033[0m\n";
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
	std::cout << "\n\033[1;33m=============== Relatorio geral de namespaces ==============\033[0m\n";
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

// Medir overhead de criação de namespaces
void measureNamespaceOverhead() {

	std::cout << "\n\033[1;33m========= Medindo overhead de criacao de namespaces ========\033[0m\n";

	const int iteracoes = 10;
	std::vector<double> tempos; // Vetor para guardar os tempos

	for (int i = 0; i < iteracoes; ++i) {
		struct timespec start, end;
		clock_gettime(CLOCK_MONOTONIC, &start);

		pid_t pid = fork();
		if (pid == 0) {
			// processo filho
			unshare(CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWNS);
			_exit(0);
		}
		else if (pid > 0) {
			waitpid(pid, nullptr, 0);
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		double diff = (end.tv_sec - start.tv_sec) * 1e6 + (end.tv_nsec - start.tv_nsec) / 1e3;
		tempos.push_back(diff); // Guarda o tempo no vetor

		// Imprime o resultado individual
		std::cout << " - Criacao " << (i + 1) << "/" << iteracoes << ": "
			<< formatarNumero(diff) << " microssegundos\n";
	}
	// --- Seção de Resumo ---
	// Calcula as estatísticas
	double somaTempo = std::accumulate(tempos.begin(), tempos.end(), 0.0);
	double media = (somaTempo / iteracoes);
	double minTempo = *std::min_element(tempos.begin(), tempos.end()); // Acha o menor tempo
	double maxTempo = *std::max_element(tempos.begin(), tempos.end()); // Acha o maior tempo

	std::cout << "\n\033[1;33m========================== Resumo ==========================\033[0m\n";
	std::cout << " Total de iteracoes: " << iteracoes << "\n";
	std::cout << " - Menor tempo (Fastest): " << formatarNumero(minTempo) << " microssegundos\n";
	std::cout << " - Maior tempo (Slowest): " << formatarNumero(maxTempo) << " microssegundos\n";
	std::cout << " - Overhead medio: " << formatarNumero(media) << " microssegundos\n";
}

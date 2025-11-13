#include "cgroup.h"
#include "namespace.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <vector>
#include <limits>
#include <algorithm>
#include <set>
#include <unistd.h>

namespace fs = std::filesystem;

struct ProcessInfo {
    int pid;
    std::string name;
};

bool processoExiste(int PID) {
    std::filesystem::path dir = "/proc/" + std::to_string(PID);
    return std::filesystem::exists(dir) && std::filesystem::is_directory(dir);
}

std::vector<ProcessInfo> listarProcessos() {
    std::vector<ProcessInfo> lista;

    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;

        const std::string dir = entry.path().filename().string();
        if (!std::all_of(dir.begin(), dir.end(), ::isdigit)) continue; // só números (PIDs)

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

        // valida entrada
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

// =========================================
// FUNÇÃO PARA O GERENCIADOR DE CGROUP
// =========================================
void cgroupManager() {
    CGroupManager manager;

	std::cout << "\033[1;36m"; // ciano em negrito
	std::cout << "\n============================================================\n";
	std::cout << "                         CGroup Manager                      \n";
	std::cout << "============================================================\n";
	std::cout << "\033[0m"; // reseta as cores pro resto do texto

    std::string cgroupName;
    std::cout << "\n Nome do cgroup experimental: ";
    std::cin >> cgroupName;

    if (!manager.createCGroup(cgroupName)) {
        std::cerr << "Falha ao criar cgroup.\n";
        return;
    }

    int pid = escolherPID();
    if (!manager.moveProcessToCGroup(cgroupName, pid)) {
        std::cerr << "Falha ao mover o processo para o cgroup.\n";
        return;
    }

    double cores;
    std::cout << "Limite de CPU (em núcleos, ex: 0.5, 1.0, -1 para ilimitado): ";
    std::cin >> cores;
    manager.setCpuLimit(cgroupName, cores);

    size_t memBytes;
    std::cout << "Limite de memória (em bytes, ex: 1000000000): ";
    std::cin >> memBytes;
    manager.setMemoryLimit(cgroupName, memBytes);

    std::cout << "\n--- Relatório de uso ---\n";

    // CPU
    auto cpu = manager.readCpuUsage(cgroupName);
    if (cpu.count("usage_usec"))
        std::cout << "CPU total usada (µs): " << cpu["usage_usec"] << "\n";
    if (cpu.count("user_usec"))
        std::cout << "Tempo em modo usuário (µs): " << cpu["user_usec"] << "\n";
    if (cpu.count("system_usec"))
        std::cout << "Tempo em modo kernel (µs): " << cpu["system_usec"] << "\n";

    // Memória
    auto mem = manager.readMemoryUsage(cgroupName);
    if (mem.count("memory.current"))
        std::cout << "Memória atual (bytes): " << mem["memory.current"] << "\n";

    std::cout << "\nEstatísticas de BlkIO:\n";
    auto blk = manager.readBlkIOUsage(cgroupName);
    if (blk.empty())
        std::cout << "(sem atividade de I/O registrada)\n";
}

// =========================================
// FUNÇÃO PARA O ANALISADOR DE NAMESPACE
// =========================================
void namespaceAnalyzer(){
	int sub;
	do {
		std::cout << "\033[1;36m"; // ciano em negrito
		std::cout << "\n============================================================\n";
		std::cout << "                       Namespace Analyzer                    \n";
		std::cout << "============================================================\n";
		std::cout << "\033[0m"; // reseta as cores pro resto do texto
		std::cout << "\033[1m"; // deixa opções em negrito
		std::cout << " 1. Listar namespaces de um processo\n";
		std::cout << " 2. Comparar namespaces entre dois processos\n";
		std::cout << " 3. Procurar processos em um namespace especifico\n";
		std::cout << " 4. Relatório geral de namespaces\n";
		std::cout << " 5. Medir overhead de criação\n";
		std::cout << " 0. Voltar ao menu inicial\n";
		std::cout << "------------------------------------------------------------\n";
		std::cout << "Escolha: ";
		std::cout << "\033[0m"; // reseta as cores pro resto do texto
		std::cin >> sub;

		if (sub == 1) {
			std::cout << "\n\033[1;33m=================== Processos disponiveis ==================\033[0m\n";
			int pid = escolherPID();
			listNamespaces(pid);
		}

		else if (sub == 2) {
			std::cout << "\n\033[1;33m=================== Processos disponiveis ==================\033[0m\n";
			auto processos = listarProcessos();
			for (const auto& p : processos)
				std::cout << "PID: " << p.pid << "\tNome: " << p.name << "\n";

			int pid1, pid2;
			std::cout << "\nDigite os dois PIDs separados por espaço: ";
			std::cin >> pid1 >> pid2;
			compareNamespaces(pid1, pid2);
		}

		else if (sub == 3) {
			std::cout << "\n\033[1;33m============== Tipos de Namespaces disponiveis =============\033[0m\n";
			std::vector<std::string> tipos = { "ipc", "mnt", "net", "pid", "user", "uts", "cgroup" };
			for (const auto& t : tipos)
				std::cout << "- " << t << "\n";

			std::string tipo;
			std::cout << "\nDigite o tipo de namespace (ex: net): ";
			std::cin >> tipo;

			std::cout << "\n Buscando namespaces do tipo '" << tipo << "'...\n";

			// Listar valores únicos disponíveis para o tipo
			std::set<std::string> idsDisponiveis;
			for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
				std::string pidStr = entry.path().filename();
				if (!std::all_of(pidStr.begin(), pidStr.end(), ::isdigit)) continue;

				std::string nsPath = "/proc/" + pidStr + "/ns/" + tipo;
				char buf[256];
				ssize_t len = readlink(nsPath.c_str(), buf, sizeof(buf) - 1);
				if (len != -1) {
					buf[len] = '\0';
					idsDisponiveis.insert(std::string(buf));
				}
			}

			if (idsDisponiveis.empty()) {
				std::cout << "Nenhum namespace desse tipo foi encontrado.\n";
				continue;
			}

			std::cout << "\n\033[1;33m=============== Namespaces disponiveis (" << tipo << ") ==============\033[0m\n";
			int count = 0;
			for (const auto& id : idsDisponiveis) {
				std::cout << " " << ++count << ". " << id << "\n";
				if (count >= 10) {
					std::cout << "... (mostrando apenas os 10 primeiros)\n";
					break;
				}
			}

			std::string idEscolhido;
			std::cout << "\nDigite o valor exato do namespace (ex: 4026531993): ";
			std::cin >> idEscolhido;
			findProcessesInNamespace(tipo, idEscolhido);
		}

		else if (sub == 4) {
			reportSystemNamespaces();
		}

		else if (sub == 5) {
			measureNamespaceOverhead();
		}

		else if (sub == 0) {
			std::cout << "Voltando ao menu principal...\n";
		}

		else {
			std::cout << "Opção inválida!\n";
		}

	} while (sub != 0);

}

int main() {
	int opcao;
	do {
		std::cout << "\n\033[0;4;31m===================== RESOURCE MONITOR =====================\033[0m\n";
		std::cout << "\033[1m";
		std::cout << " 1. Gerenciar Cgroups\n";
		std::cout << " 2. Analisar Namespaces\n";
		std::cout << " 0. Sair\n";
		std::cout << " Escolha: ";
		std::cin >> opcao;
		std::cout << "\033[0m";

		switch (opcao) {
			case 1:
				cgroupManager();
				break;
			
			case 2: {
				namespaceAnalyzer();
				break;
			}

			case 0:
				std::cout << "Encerrando...\n";
				break;

			default:
				std::cout << "Opção inválida!\n";
		}

	} while (opcao != 0);
}

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

namespace fs = std::filesystem;

struct ProcessInfo {
	int pid;
	std::string name;
};

bool processoExiste(int PID) {
	std::filesystem::path dir = "/proc/" + std::to_string(PID);
	return std::filesystem::exists(dir) && std::filesystem::is_directory(dir);
}

void salvarMedicoesCSV(const StatusProcesso& medicao, const calculoMedicao& calculado)
{
    // Obtém o diretório atual do programa
    std::filesystem::path base = std::filesystem::current_path();
    // Sobe 1 nível na hierarquia de pastas
    base = base.parent_path();
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

        if (sub == 1) {
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

void cgroupManager() {
    CGroupManager manager;

    std::cout << "\033[1;36m";
    std::cout << "\n============================================================\n";
    std::cout << "                         CGroup Manager                      \n";
    std::cout << "============================================================\n";
    std::cout << "\033[0m";

    std::string cgroupName = "exp_" + std::to_string(time(nullptr));
    std::cout << "Nome do cgroup experimental: " << cgroupName << ".\n";

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

    auto cpu = manager.readCpuUsage(cgroupName);
    if (cpu.count("usage_usec"))
        std::cout << "CPU total usada (µs): " << cpu["usage_usec"] << "\n";
    if (cpu.count("user_usec"))
        std::cout << "Tempo em modo usuário (µs): " << cpu["user_usec"] << "\n";
    if (cpu.count("system_usec"))
        std::cout << "Tempo em modo kernel (µs): " << cpu["system_usec"] << "\n";

    auto mem = manager.readMemoryUsage(cgroupName);
    if (mem.count("memory.current"))
        std::cout << "Memória atual (bytes): " << mem["memory.current"] << "\n";

    std::cout << "\nEstatísticas de BlkIO:\n";
    auto blk = manager.readBlkIOUsage(cgroupName);

    if (blk.empty()) {
        std::cout << "(sem atividade de I/O registrada)\n";
        return;
    }

    for (const auto& entry : blk) {
        if (entry.rbytes == 0 && entry.wbytes == 0 && entry.dbytes == 0)
            continue;

        std::cout << "  Device " << entry.major << ":" << entry.minor << "\n";

        auto fmtBytes = [](uint64_t b) {
            const char* suf[] = { "B", "KB", "MB", "GB", "TB" };
            int i = 0;
            double v = b;
            while (v > 1024 && i < 4) { v /= 1024; i++; }
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << v << " " << suf[i];
            return oss.str();
            };

        std::cout << "    Read:    " << fmtBytes(entry.rbytes)
            << "  (" << entry.rios << " ops)\n";
        std::cout << "    Write:   " << fmtBytes(entry.wbytes)
            << "  (" << entry.wios << " ops)\n";
        std::cout << "    Discard: " << fmtBytes(entry.dbytes)
            << "  (" << entry.dios << " ops)\n\n";
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

// =========================================
// FUNÇÃO PARA O ANALISADOR DE NAMESPACE
// =========================================
void namespaceAnalyzer() {
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
		std::cout << " 0. Voltar ao menu inicial\n";
		std::cout << "------------------------------------------------------------\n";
		std::cout << "Escolha: ";
		std::cout << "\033[0m"; // reseta as cores pro resto do texto
		std::cin >> sub;

		if (sub == 1) {
			int pid = escolherPID();
			listNamespaces(pid);
		}

		else if (sub == 2) {
			auto processos = listarProcessos();

			std::cout << "\n\033[1;33m=================== Processos disponiveis ==================\033[0m\n";
			for (const auto& p : processos) {
				std::cout << "PID: " << std::left << std::setw(25) << p.pid << "\tNome: " << p.name << "\n";
			}

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
			gerarRelatorioGeralCompleto();
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
		std::cout << " 3. Perfilador de Recursos\n";
		std::cout << " 4. Executar Experimentos\n";
		std::cout << " 0. Sair\n";
		std::cout << " Escolha: ";
		std::cin >> opcao;
		std::cout << "\033[0m";

		switch (opcao) {
		case 1:
			cgroupManager();
			break;

		case 2:
			namespaceAnalyzer();
			break;

		case 3: {
			resourceProfiler();
			break;
		}

		case 4: {
			executarExperimentos();
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

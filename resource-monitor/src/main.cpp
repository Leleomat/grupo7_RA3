#include "cgroup.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <cctype>
#include <vector>
#include <limits>
#include <algorithm>

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

    std::cout << "\n=== Processos disponíveis ===\n";
    for (const auto& p : processos) {
        std::cout << "PID: " << p.pid << "\tNome: " << p.name << "\n";
    }

    int pid;
    bool valido = false;

    do {
        std::cout << "\nDigite o PID do processo para mover: ";
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

void cgroupManager() {
    CGroupManager manager;

    std::string cgroupName;
    std::cout << "Nome do cgroup experimental: ";
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

int main() {
    cgroupManager();
}

﻿#include "cgroup.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

namespace fs = std::filesystem;

// ============================================================================
// Implementação original — NÃO ALTERADA
// ============================================================================

CGroupManager::CGroupManager(const std::string& path) : basePath(path) {}

bool CGroupManager::createCGroup(const std::string& name) {
    try {
        fs::create_directory(basePath + name);
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Erro ao criar cgroup: " << e.what() << std::endl;
        return false;
    }
}

bool CGroupManager::moveProcessToCGroup(const std::string& name, int pid) {
    std::ofstream tasks(basePath + name + "/cgroup.procs");
    if (!tasks.is_open()) return false;
    tasks << pid;
    return true;
}

bool CGroupManager::setCpuLimit(const std::string& name, double cores) {
    std::ofstream cpuMax(basePath + name + "/cpu.max");
    if (!cpuMax.is_open()) return false;

    const int period = 100000;
    int quota = static_cast<int>(cores * period);

    if (cores <= 0) quota = -1; // sem limite

    cpuMax << quota << " " << period;
    return true;
}

bool CGroupManager::setMemoryLimit(const std::string& name, size_t bytes) {
    std::ofstream memMax(basePath + name + "/memory.max");
    if (!memMax.is_open()) return false;
    memMax << bytes;
    return true;
}

std::map<std::string, double> CGroupManager::readCpuUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/cpu.stat");
    std::map<std::string, double> stats;
    std::string key;
    double val;

    while (f >> key >> val) {
        stats[key] = val;
    }
    return stats;
}

std::map<std::string, size_t> CGroupManager::readMemoryUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/memory.current");
    std::map<std::string, size_t> stats;

    size_t val;
    if (f >> val)
        stats["memory.current"] = val;

    return stats;
}

std::vector<BlkIOStats> CGroupManager::readBlkIOUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/io.stat");
    std::vector<BlkIOStats> list;

    if (!f.is_open()) {
        std::cerr << "Falha ao abrir io.stat\n";
        return list;
    }

    std::string line;

    while (std::getline(f, line)) {
        if (line.empty()) continue;

        BlkIOStats s{};
        std::istringstream iss(line);

        // formato:
        // 8:0 rbytes=123 wbytes=456 rios=3 wios=4 dbytes=0 dios=0
        iss >> s.major;
        iss.ignore(1); // ':' 
        iss >> s.minor;

        std::string kv;
        while (iss >> kv) {
            auto pos = kv.find('=');
            if (pos == std::string::npos) continue;
            std::string key = kv.substr(0, pos);
            uint64_t val = std::stoull(kv.substr(pos + 1));

            if (key == "rbytes") s.rbytes = val;
            else if (key == "wbytes") s.wbytes = val;
            else if (key == "rios") s.rios = val;
            else if (key == "wios") s.wios = val;
            else if (key == "dbytes") s.dbytes = val;
            else if (key == "dios") s.dios = val;
        }

        list.push_back(s);
    }

    return list;
}

void CGroupManager::runCpuThrottlingExperiment() {
    std::string cg = "exp3_" + std::to_string(time(nullptr));
    this->createCGroup(cg);

    std::cout << "\n===== EXPERIMENTO 3 — CPU THROTTLING =====\n";

    pid_t pid = fork();

    if (pid == 0) {
        // ---- FILHO ----
        CGroupManager mgr(this->basePath);
        mgr.moveProcessToCGroup(cg, getpid());

        while (true) {
            asm volatile("" ::: "memory"); // loop infinito de CPU
        }

        exit(0);
    }

    // ---- PAI ----
    std::vector<double> limites = { 0.25, 0.5, 1.0, 2.0 };

    for (double lim : limites) {
        this->setCpuLimit(cg, lim);

        auto t0 = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto t1 = std::chrono::high_resolution_clock::now();

        double secs = std::chrono::duration<double>(t1 - t0).count();

        auto stat = this->readCpuUsage(cg);
        double usage_sec = stat["usage_usec"] / 1e6;

        double cpuPercent = (usage_sec / secs) * 100.0;
        double expected = lim * 100.0;
        double desvio = ((cpuPercent - expected) / expected) * 100.0;

        std::cout << "\n--- Limite: " << lim << " cores ---\n";
        std::cout << "CPU medido: " << cpuPercent << "%\n";
        std::cout << "CPU esperado: " << expected << "%\n";
        std::cout << "Desvio: " << desvio << "%\n";
    }

    kill(pid, SIGKILL);

    int status = 0;
    waitpid(pid, &status, 0);
}

void CGroupManager::runMemoryLimitExperiment() {
    std::string cg = "exp4_" + std::to_string(time(nullptr));
    this->createCGroup(cg);

    // Limite fixo de 100 MB
    this->setMemoryLimit(cg, 100ull * 1024 * 1024);

    std::cout << "\n===== EXPERIMENTO 4 — LIMITE DE MEMÓRIA (cgroups v2) =====\n";

    pid_t pid = fork();

    if (pid == 0) {
        // ---- FILHO ----
        CGroupManager mgr(this->basePath);
        mgr.moveProcessToCGroup(cg, getpid());

        std::vector<void*> blocos;
        size_t total = 0;
        const size_t passo = 20 * 1024 * 1024; // 20 MB

        while (true) {
            void* b = malloc(passo);
            if (!b) break;    // Caso o malloc falhe, encerramos
            memset(b, 0, passo);

            blocos.push_back(b);
            total += passo;

            auto mem = mgr.readMemoryUsage(cg);

            std::cout << "Alocado: " << (total / (1024 * 1024))
                << " MB | memory.current=" << mem["memory.current"]
                << "\n";

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        exit(0); // Só acontece se o filho terminar sem OOM kill
    }

    // ---- PAI ----
    int status;
    waitpid(pid, &status, 0);

    // Ler memory.events
    size_t oom = 0, oom_kill = 0, high = 0;
    {
        std::ifstream fe(this->basePath + cg + "/memory.events");
        std::string k;
        size_t v;
        while (fe >> k >> v) {
            if (k == "oom")      oom = v;
            if (k == "oom_kill") oom_kill = v;
            if (k == "high")     high = v;
        }
    }

    // Ler memory.peak (pico real medido pelo kernel)
    size_t peak = 0;
    {
        std::ifstream fp(this->basePath + cg + "/memory.peak");
        if (fp) fp >> peak;
    }

    std::cout << "\n===== RESULTADO FINAL =====\n";
    std::cout << "oom       = " << oom << "\n";
    std::cout << "oom_kill  = " << oom_kill << "\n";
    std::cout << "high      = " << high << "\n";
    std::cout << "Máximo de memória alcançada = " << peak / (1024 * 1024) << " MB\n";
}




#include "cgroup.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

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

    // Em cgroups v2, formato: "quota period"
    // Exemplo: 50000 100000 = 0.5 core
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

// Leitura básica das métricas
std::map<std::string, double> CGroupManager::readCpuUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/cpu.stat");
    std::map<std::string, double> stats;
    std::string key; double val;
    while (f >> key >> val) stats[key] = val;
    return stats;
}

std::map<std::string, size_t> CGroupManager::readMemoryUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/memory.current");
    std::map<std::string, size_t> stats;
    size_t val; if (f >> val) stats["memory.current"] = val;
    return stats;
}

std::map<std::string, double> CGroupManager::readBlkIOUsage(const std::string& name) {
    std::ifstream f(basePath + name + "/io.stat");
    std::map<std::string, double> stats;
    std::string line;
    while (std::getline(f, line))
        std::cout << "BlkIO: " << line << std::endl;
    return stats;
}

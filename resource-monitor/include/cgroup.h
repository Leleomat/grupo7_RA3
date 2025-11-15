#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

struct BlkIOStats {
    int major;
    int minor;
    uint64_t rbytes;
    uint64_t wbytes;
    uint64_t rios;
    uint64_t wios;
    uint64_t dbytes;
    uint64_t dios;
};

class CGroupManager {
public:
    std::string basePath;
    explicit CGroupManager(const std::string& path = "/sys/fs/cgroup/");

    bool createCGroup(const std::string& name);
    bool moveProcessToCGroup(const std::string& name, int pid);
    bool setCpuLimit(const std::string& name, double cores);
    bool setMemoryLimit(const std::string& name, size_t bytes);

    std::map<std::string, double> readCpuUsage(const std::string& name);
    std::map<std::string, size_t> readMemoryUsage(const std::string& name);
    std::vector<BlkIOStats> readBlkIOUsage(const std::string& name);

    // Experimento 3 — Throttling de CPU
    void runCpuThrottlingExperiment();

    // Experimento 4 — Limite de Memória
    void runMemoryLimitExperiment();
};

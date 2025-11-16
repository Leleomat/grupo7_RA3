#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

// Estrutura responsável por armazenar os dados de BlkIO de um processo em um CGroup
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

// Classe CGroupManager que possuí todas as funções atribuídas
class CGroupManager {
public:
    std::string basePath; // Caminho base de cgroup
    explicit CGroupManager(const std::string& path = "/sys/fs/cgroup/");

    bool createCGroup(const std::string& name); // Função que cria o CGroup
    bool moveProcessToCGroup(const std::string& name, int pid); // Função que move processo para CGroup
    bool setCpuLimit(const std::string& name, double cores); // Função que faz o set do CPU limite
    bool setMemoryLimit(const std::string& name, size_t bytes); // Função que faz o set da memória limite

    std::map<std::string, double> readCpuUsage(const std::string& name); // Função que mapeia o uso da CPU
    std::map<std::string, size_t> readMemoryUsage(const std::string& name); // Função que mapeia o uso da memória
    std::vector<BlkIOStats> readBlkIOUsage(const std::string& name); // Função que mapeia o uso de IO

    // Experimento 3 — Throttling de CPU
    void runCpuThrottlingExperiment();
    uint64_t readIterationsFromChild(pid_t pid); 

	// Experimento 4 — Limite de Memória
	void runMemoryLimitExperiment();
};

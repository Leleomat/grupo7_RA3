#pragma once
#include <string>
#include <map>

class CGroupManager {
public:
    explicit CGroupManager(const std::string& path = "/sys/fs/cgroup/");

    bool createCGroup(const std::string& name);
    bool moveProcessToCGroup(const std::string& name, int pid);
    bool setCpuLimit(const std::string& name, double cores);
    bool setMemoryLimit(const std::string& name, size_t bytes);

    std::map<std::string, double> readCpuUsage(const std::string& name);
    std::map<std::string, size_t> readMemoryUsage(const std::string& name);
    std::map<std::string, double> readBlkIOUsage(const std::string& name);

private:
    std::string basePath;
};

#pragma once
#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <string>
#include <vector>

// Estrutura que armazena informações sobre um namespace de um processo
struct NamespaceInfo {
	std::string type;
	std::string id;
};

// Funções principais
void listNamespaces(int pid);
void compareNamespaces(int pid1, int pid2);
void findProcessesInNamespace(const std::string& nsType, const std::string& nsId);
void reportSystemNamespaces();
void measureNamespaceOverhead();

#endif

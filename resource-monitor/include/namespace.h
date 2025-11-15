// Início do "Include Guard" padrão do C++.
// Isso previne erros de "definição múltipla" caso o arquivo seja incluído mais de uma vez.
// Se 'NAMESPACE_H' ainda não foi definido...
#ifndef NAMESPACE_H
// ...então defina 'NAMESPACE_H'.
#define NAMESPACE_H

#include <string> // Necessário para usar 'std::string'.
#include <vector> // Necessário para usar 'std::vector'

// Estrutura que armazena informações sobre um namespace de um processo
// Define um tipo de dado personalizado (struct) para agrupar as informações de um namespace.
struct NamespaceInfo {
	// O tipo do namespace (ex: "net", "pid", "mnt").
	std::string type;
	// O identificador único do namespace (ex: "net:[4026531993]").
	std::string id;
};

// Funções principais
// Abaixo estão as "declarações" ou "protótipos" das funções.
// Isso informa ao compilador que essas funções existem em algum lugar (no .cpp).
// O "linking" (ligação) conectará esta declaração com o código real da função.

void listNamespaces(int pid); // Declaração da função que lista namespaces de um PID específico.
void compareNamespaces(int pid1, int pid2); // Declaração da função que compara os namespaces entre dois PIDs.
void findProcessesInNamespace(const std::string& nsType, const std::string& nsId); // Declaração da função que encontra todos os PIDs que pertencem a um namespace específico.
void reportSystemNamespaces(); // Declaração da função que conta quantos namespaces únicos de cada tipo existem no sistema.
void executarExperimentoIsolamento(); // Declaração da função que executa o "Experimento 2" (overhead e isolamento).
void reportProcessCountsPerNamespace(); // Declaração da função que conta quantos processos estão em cada namespace único.
void gerarRelatorioGeralCompleto(); // Declaração da função que gera um relatório completo combinando outros relatórios.

// Fim do bloco do "Include Guard" (#ifndef NAMESPACE_H).
#endif
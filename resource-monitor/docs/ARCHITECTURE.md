# ARCHITECTURE.md
## Arquitetura do Projeto — *resource-monitor*
### Grupo 7 – RA3 (PUCPR)

---

## 1. Visão Geral do Sistema

O **resource-monitor** é um sistema de profiling e análise de recursos para processos em Linux. O objetivo é monitorar e validar métricas de **CPU**, **Memória**, **Namespaces**, **I/O** e **Network**, além de experimentar mecanismos de isolamento e limitação do kernel (namespaces e cgroups). O sistema foi projetado para ser modular, reprodutível e didático — perfeito para demonstrar como containers e sistemas de orquestração utilizam primitivas do kernel.

Principais capacidades:
- Coleta de métricas por PID (CPU user/sys, CPU%, per-core, RSS, VSZ, read/write bytes, syscalls).
- Análise de namespaces (listar, comparar, mapear processos para namespaces, medir overhead).
- Gerência de cgroups (criação de cgroups experimentais, aplicação de limites CPU/mem/IO, leitura de métricas).
- Exportação em CSV.

---

## 2. Estrutura do Repositório

```
resource-monitor/
├── README.md
├── Makefile
├── resource-monitor.sln
├── docs/
│   └── ARCHITECTURE.md
│   └── (imagens utilizadas para o README)
├── include/
│   ├── monitor.h
│   ├── namespace.h
│   └── cgroup.h
├── src/
│   ├── main.cpp
│   ├── monitor.cpp
│   ├── cpu_monitor.cpp
│   ├── memory_monitor.cpp
│   ├── io_monitor.cpp
│   ├── namespace_analyzer.cpp
│   └── cgroup_manager.cpp
├── tests/
│   ├── test_cpu.cpp
│   ├── test_memory.cpp
│   └── test_io.cpp
└── scripts/
    ├── visualize.py
    └── compare_tools.sh
```

> Observação: todos os arquivos de código principal usam extensão `.cpp` conforme pedido.

---

## 3. Descrição Detalhada dos Módulos e Arquivos

### include/monitor.h
- **Responsabilidade:** Definir as estruturas de dados e as funções de coleta para o monitoramento de processos individuais. Sua principal tarefa é ler dados brutos do filesystem `/proc` (ex: `/proc/[PID]/stat`, `/proc/[PID]/status`, `/proc/[PID]/io`) e armazená-los para análise.
- **Principais tipos:**
- `struct StatusProcesso`: É a estrutura de dados central do _profiler_. Ela armazena um "instantâneo" (snapshot) de todas as métricas brutas de um processo em um determinado momento, incluindo estatísticas de CPU (`utime`, `stime`), Memória (`vmSize`, `vmRss`), I/O (`bytesLidos`, `bytesEscritos`) e Rede (`bytesRxfila`, `bytesTxfila`), conforme solicitado.
- `struct calculoMedicao`: Armazena métricas _derivadas_ (calculadas), como taxas e porcentagens (ex: `usoCPU`, `taxaLeituraDisco`). Estas são calculadas comparando dois `StatusProcesso` tirados em momentos diferentes, atendendo ao requisito de "Calcular CPU% e taxas de I/O".
- **Funções públicas:**
- `processoExiste(int PID)`, `temPermissao(int PID)`: Funções de validação e guarda. Elas garantem que o PID alvo existe e que o monitor tem permissão de leitura, tratando erros como "processo inexistente" e "permissões". 
- `coletorCPU(StatusProcesso &medicao)`, `coletorMemoria(...)`, `coletorIO(...)`, `coletorNetwork(...)`: O núcleo do _profiler_. Cada função é responsável por preencher as partes relevantes da struct `StatusProcesso` passada por referência.
- `overheadMonitoramento()`, `cargaExecutar()`: Funções dedicadas ao Experimento 1. `cargaExecutar` é o _workload_ de referência, e `overheadMonitoramento` orquestra a medição do impacto do _profiler_.
- **Contrato:** O consumidor (ex: `main.cpp`) deve primeiro validar um PID com `processoExiste`. Em seguida, pode criar um loop que, a cada intervalo de tempo, preenche uma struct `StatusProcesso` usando as funções `coletor...` e calcula as métricas derivadas (armazenadas em `calculoMedicao`) para exibir ao usuário.

### include/namespace.h
- **Responsabilidade:** Abstrair todas as interações com o sistema de _namespaces_ do Linux. Ele fornece uma interface procedural para consultar, comparar e gerar relatórios sobre o isolamento dos processos.
- **Leituras:** O módulo opera quase exclusivamente lendo os links simbólicos no filesystem `/proc`, especificamente no diretório `/proc/[PID]/ns/`. Por exemplo, `readlink("/proc/123/ns/net")` retorna o ID do _namespace_ de rede do processo 123.
- **Funções:**
- `struct NamespaceInfo`: Uma estrutura simples de agregação para armazenar o tipo (`net`, `pid`, etc.) e o ID (`net:[4026531993]`) de um _namespace_.
- `listNamespaces(int pid)`: Implementa o requisito "Listar todos os namespaces de um processo".
- `compareNamespaces(int pid1, int pid2)`: Implementa o requisito "Comparar namespaces entre dois processos".
- `findProcessesInNamespace(...)`: Implementa o requisito "Encontrar processos em um namespace específico".
- `reportSystemNamespaces()`, `reportProcessCountsPerNamespace()`, `gerarRelatorioGeralCompleto()`: Implementam os requisitos de "Gerar relatório de namespaces do sistema" e reportar o "Número de processos por namespace no sistema".
- `executarExperimentoIsolamento()`: Função de alto nível que orquestra o Experimento 2  , usando as outras funções para "Validar efetividade do isolamento" e "Medir tempo de criação"

### include/cgroup.h
- **Responsabilidade:** Encapsular toda a lógica de manipulação e leitura de _Control Groups_ (cgroups). Diferente do `namespace.h`, este módulo é orientado a objetos (com a classe `CGroupManager`), tratando um _cgroup_ como um recurso a ser gerenciado. Ele interage com o filesystem `/sys/fs/cgroup/`.
- **Funções:**

- -   _Funções de Modificação (Setters):_
	    - `createCGroup(const std::string& name)`: Implementa "Criar cgroup experimental". Isso é feito criando um novo diretório em `/sys/fs/cgroup/[controlador]/`.
	    - `moveProcessToCGroup(const std::string& name, int pid)`: Implementa "Mover processo para cgroup". Isso é feito escrevendo o PID no arquivo `cgroup.procs` (ou `tasks`) do _cgroup_.
	    - `setCpuLimit(...)`, `setMemoryLimit(...)`: Implementam "Aplicar limites de CPU e Memória". Isso é feito escrevendo valores nos arquivos de controle (ex: `memory.limit_in_bytes` ou `cpu.cfs_quota_us`).
       
        
- - _Funções de Leitura (Getters):_
    - `readCpuUsage(...)`, `readMemoryUsage(...)`, readBlkIOUsage(...)`: Implementam "Ler métricas de CPU, Memory e BIkIO cgroups". Isso é feito lendo os arquivos de estatísticas (ex: `memory.usage_in_bytes`, `cpuacct.usage`, `blkio.throttle.io_service_bytes`).
	- `struct BlkIOStats`: Estrutura de ajuda para deserializar os dados complexos lidos do controlador `blkio`. 

- - _Funções de Experimento:_   
	- `runCpuThrottlingExperiment()`: Orquestra o Experimento 3, usando as funções de `set` e `read` para comparar o limite configurado com o uso real.
	- `runMemoryLimitExperiment()`: Orquestra o Experimento 4, aplicando um limite e monitorando o comportamento do processo (ex: OOM killer).
- **Comportamento:** O `CGroupManager` é instanciado com o caminho base para o _cgroup_ (padrão `/sys/fs/cgroup/`). Cada chamada de função (ex: `setCpuLimit("meu_grupo", 0.5)`) é traduzida em uma operação de arquivo, como escrever um valor no arquivo `/sys/fs/cgroup/cpu/meu_grupo/cpu.cfs_quota_us`.

---

### src/main.cpp
- **Responsabilidade:** É o ponto de entrada (entry point) e orquestrador principal do programa. Ele é responsável por:
1.  Apresentar o menu principal interativo ao usuário.
2.  Invocar os sub-módulos corretos (Profiler, Analyzer, Cgroup, Experimentos) com base na escolha do usuário.
3.  Conter a lógica de alto nível e os sub-menus de cada componente principal (ex: as funções `resourceProfiler()`, `namespaceAnalyzer()`, `cgroupManager()`).
4.  Fornecer funções utilitárias globais essenciais, como `listarProcessos()`, `escolherPID()` (para seleção de processo) e `salvarMedicoesCSV()` (para exportação de dados).
- **Workflow:**
	- A função `main()` entra em um loop `do-while`, exibindo o menu principal (Gerenciar Cgroups, Analisar Namespaces, Perfilador de Recursos, Executar Experimentos).
	-   O usuário seleciona uma opção (ex: "3" para o Profiler). O `switch` direciona para a função correspondente (ex: `resourceProfiler()`).
	-   A função de sub-menu (ex: `resourceProfiler()`) então solicita ao usuário as informações contextuais necessárias, como o **PID** (usando `escolherPID()`) e o **intervalo** de monitoramento.
	-   Essa função entra em seu próprio loop de execução (ex: `while(true)` no `resourceProfiler`). Dentro desse loop, ela chama as funções coletoras dos módulos (`coletorCPU`, `coletorMemoria`, etc.).
	-   Após a segunda medição (para ter um delta), ela calcula as métricas derivadas (como CPU% e taxas de I/O).
	-   Os resultados são exibidos em uma tabela formatada no console e também salvos em um arquivo `.csv` na pasta `docs/` através da função `salvarMedicoesCSV`.
- **Exemplos de uso documentados:**
	- Monitoramento de Processo (via `resourceProfiler`): O usuário seleciona '3', escolhe um PID da lista e define um intervalo. O programa exibe uma tabela de métricas (CPU, Memória, I/O) que se atualiza no intervalo definido, ao mesmo tempo que gera um arquivo `docs/dados[PID].csv`.
	- Gerenciamento de Cgroup (via `cgroupManager`): O usuário seleciona '1'. O programa automaticamente cria um cgroup experimental, pede um PID para mover para ele, e solicita limites de CPU e Memória. Em seguida, exibe um relatório único do estado atual do cgroup.
	- Análise de Namespace (via `namespaceAnalyzer`): O usuário seleciona '2' e entra em um sub-menu onde pode, por exemplo, escolher '1. Listar namespaces', que então pede um PID e chama a função `listNamespaces`.
	- Execução de Experimentos (via `executarExperimentos`): O usuário seleciona '4' e vê um menu dos experimentos obrigatórios. Ao escolher '3. Experimento nº2', por exemplo, o programa chama diretamente a função `executarExperimentoIsolamento`.

### src/cpu_monitor.cpp
- **Responsabilidade:** Implementar as funções de coleta do Resource Profiler relacionadas à CPU e fornecer a lógica para o Experimento 1 (Overhead de Monitoramento).
- **Algoritmo:**
	1.  **Coleta de Tempos (`coletorCPU`):** A função lê o arquivo `/proc/[PID]/stat`. Como o nome do processo (segundo campo) pode conter espaços e está entre parênteses, o _parser_ localiza o **último** caractere ')' e, a partir daí, lê os campos de forma posicional. Ele coleta o 14º campo (`utime`, tempo em modo usuário) e o 15º (`stime`, tempo em modo kernel). Esses valores, medidos em _jiffies_ (ticks de clock), são divididos por `sysconf(_SC_CLK_TCK)` para serem convertidos em segundos.
    2.  **Coleta de Threads/Contexto (`coletorCPU`):** A função lê o arquivo `/proc/[PID]/status` linha por linha, procurando pelas chaves `Threads:`, `voluntary_ctxt_switches:`, e `nonvoluntary_ctxt_switches:`. Os valores numéricos dessas linhas são extraídos e armazenados na struct `StatusProcesso`.
-   Atenção: Este arquivo também contém a lógica completa do Experimento 1 (`overheadMonitoramento`). A função `cargaExecutar()` gera um _workload_ de CPU moderado por 5 segundos. A função `overheadMonitoramento` primeiro executa essa carga como _baseline_ (sem monitoramento) e, em seguida, a executa novamente enquanto a monitora em diferentes intervalos (250ms, 500ms, 1000ms), medindo a latência da coleta e o impacto (overhead) na performance.

### src/memory_monitor.cpp
- **Responsabilidade:** Implementar as funções de coleta do Resource Profiler focadas em Memória. Sua única tarefa é preencher os campos de memória da struct `StatusProcesso` passada por referência.
- **Detalhes:** A coleta (`coletorMemoria`) é feita lendo dois arquivos de sistema distintos:
	1.  **`/proc/[PID]/status`**: Este arquivo é lido linha por linha para extrair os valores (em kB) das seguintes chaves: `VmSize:` (Memória Virtual Total), `VmRSS:` (Memória Física Residente, ou "RSS") e `VmSwap:` (Memória paginada para disco).
	2.  **`/proc/[PID]/stat`**: O mesmo arquivo usado pelo `cpu_monitor`. O _parser_ usa a mesma lógica de encontrar o último ')' e avança posicionalmente até o 10º campo (`minflt`, _minor page faults_) e o 12º campo (`majflt`, _major page faults_) para coletar as contagens de falhas de página.

### src/io_monitor.cpp
- **Responsabilidade:** Implementar os coletores de I/O de Disco e Rede para o Resource Profiler. Ele é responsável por preencher os campos de I/O e Rede da struct `StatusProcesso`.
- **Observação:** O módulo contém duas lógicas de coleta muito distintas.
	-   `coletorIO` (I/O de Disco): Faz uma leitura simples do arquivo `/proc/[PID]/io`. Ele usa `sscanf` para extrair chaves como `read_bytes` (I/O físico de disco), `write_bytes` (I/O físico de disco), `rchar` (I/O lógico, incluindo cache) e `wchar` (I/O lógico, incluindo cache), preenchendo a struct `medicao`.
	-   `coletorNetwork` (I/O de Rede): É significativamente mais complexo. Ele primeiro lista os _file descriptors_ do processo em `/proc/[PID]/fd` para encontrar todos os inodes de `socket:`. Em seguida, ele lê os arquivos `/proc/net/tcp*` e `/proc/net/udp*` para mapear inodes de socket para suas estatísticas de fila (TX/RX). Por fim, ele correlaciona os sockets do processo com as estatísticas do sistema para somar os `bytesRxfila` (bytes em fila de recepção) e `bytesTxfila` (bytes em fila de transmissão).

### src/namespace_analyzer.cpp
- **Responsabilidade:** Implementar todas as funções do "Componente 2: Namespace Analyzer" e a lógica completa do Experimento 2. Ele é a implementação central do cabeçalho `namespace.h`. Sua principal técnica é ler e analisar os links simbólicos no filesystem `/proc` (especificamente `/proc/[PID]/ns/`) para mapear, comparar e relatar o isolamento do processo.
- **Validação de isolamento:** A validação (feita em `demonstrarIsolamento`) é o procedimento central do Experimento 2. Ela usa a chamada de sistema `clone()` para criar um processo-filho (`child_main`) com as flags `CLONE_NEWPID`, `CLONE_NEWNET` e `CLONE_NEWNS`. O processo-filho então tenta executar ações privilegiadas em seu _namespace_ (como `mount("/proc", ...)`), verifica seu próprio PID (esperando ser 1) e lista suas interfaces de rede (esperando não ver `eth0`). O sucesso ou falha dessas ações é retornado ao processo-pai (`demonstrarIsolamento`) como um _bitmask_, que é então usado para imprimir a "Tabela de Isolamento Efetivo".

### src/cgroup_manager.cpp
- **Responsabilidade:** Implementar a classe `CGroupManager`, que gerencia o "Componente 3: Control Group Manager", e as lógicas dos Experimentos 3 e 4. As funções traduzem conceitos (ex: "limitar CPU") em operações de arquivo no filesystem `/sys/fs/cgroup` (ex: escrever em `cpu.max` ou `memory.max`).
- **Medições:** As funções de leitura (`readCpuUsage`, `readMemoryUsage`, `readBlkIOUsage`) são responsáveis por ler e _parsear_ os arquivos de estatísticas (`cpu.stat`, `memory.current`, `io.stat`), que fornecem dados brutos de uso do _cgroup_.
- **Orquestração de Experimentos:** O arquivo contém a lógica completa de execução dos Experimentos 3 (CPU) e 4 (Memória). Ele gerencia o ciclo de vida dos processos de teste (usando `fork()`, `kill()`, `waitpid()`), aplica os limites (`setCpuLimit`, `setMemoryLimit`), e coleta os resultados para gerar o relatório final. 
---

## 4. Permissões e Ambiente

- `sudo` é necessário para operações de criação/manipulação de namespaces e cgroups.
- Testar no Ubuntu 24.04+ (conforme enunciado). Em WSL algumas features de cgroups/I/O podem se comportar diferente.

---

## 5. Testes & Experimentos

Os scripts em `tests/` geram cargas controladas para validar cada métrica:
- `test_cpu.cpp` → cria threads com loops matemáticos para consumir CPU.
- `test_memory.cpp` → aloca blocos grandes e mantém alocação.
- `test_io.cpp` → escreve/ler arquivos para gerar I/O contínuo.

Os demais experimentos são melhores explicados e comentados no README.

---

## 6. Diagrama Rápido (ASCII)

```
[User CLI] -->   [main.cpp]
                     |
       ----------------------------------
       |             |                  |
   [monitor]    [namespace]          [cgroup]
       |             |                  |
 cpu_monitor   namespace_analyzer   cgroup_manager
 memory_monitor
 io_monitor
```


---


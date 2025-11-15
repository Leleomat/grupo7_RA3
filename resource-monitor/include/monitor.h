#pragma once // garante inclusão única do header

// ---- Estrutura que guarda o status atual de um processo ----
struct StatusProcesso{
    int PID; // identificador do processo

    // --- CPU ---
    double utime;   // tempo em modo usuário consumido pelo processo (segundos)
    double stime;   // tempo em modo kernel (sistema) consumido pelo processo (segundos)
    unsigned int threads; // número de threads ativas no processo
    unsigned int contextSwitchfree;   // trocas de contexto voluntárias
    unsigned int contextSwitchforced; // trocas de contexto forçadas

    // --- Memória ---
    unsigned long vmSize; // tamanho total de memória virtual (bytes)
    unsigned long vmRss;  // memória residente em RAM (bytes)
    unsigned long vmSwap; // memória swap usada (bytes)
    unsigned long minfault; // page faults menores (não exigem carregamento de disco)
    unsigned long mjrfault; // page faults maiores (carregam da memória secundária/disco)

    // --- I/O ---
    unsigned long bytesLidos;       // bytes efetivamente lidos do disco
    unsigned long bytesEscritos;    // bytes efetivamente escritos no disco
    unsigned long syscallLeitura;   // número de chamadas de leitura (read)
    unsigned long syscallEscrita;   // número de chamadas de escrita (write)
    unsigned long rchar;            // bytes lidos pelo processo (inclui cache)
    unsigned long wchar;            // bytes escritos pelo processo (inclui cache)

    // --- Network ---
    unsigned long bytesRxfila;      // bytes recebidos na fila de rede
    unsigned long bytesTxfila;      // bytes enviados na fila de rede
    unsigned int conexoesAtivas;    // número de conexões de rede ativas
};

// ---- Estrutura para armazenar métricas calculadas a partir do StatusProcesso ----
struct calculoMedicao{
    double usoCPU;          // percentual de CPU usado pelo processo
    double usoCPUGlobal;    // percentual de CPU usado considerando todos os núcleos
    double taxaLeituraDisco; // taxa de leitura do disco em B/s
    double taxaLeituraTotal; // taxa total de leitura (disco + cache) em B/s
    double taxaEscritaDisco; // taxa de escrita do disco em B/s
    double taxaEscritaTotal; // taxa total de escrita (disco + cache) em B/s
};

// ---- Estrutura usada pelo processo filho para enviar relatórios periódicos ao pai ----
struct RelatorioFilho {
    uint64_t bytesMovimentados; // total de bytes lidos + escritos no período
    uint64_t operacoes;         // número de iterações read+write no período
    double   latMediaMs;        // latência média das operações no período (ms)
    uint64_t timestampNs;       // timestamp em nanossegundos do relatório
};

// ---- Funções auxiliares ----
bool processoExiste(int PID);            // retorna true se o processo existe
bool temPermissao(int PID);              // retorna true se temos permissão de acesso

bool coletorCPU(StatusProcesso &medicao);     // preenche métricas de CPU
bool coletorMemoria(StatusProcesso &medicao); // preenche métricas de memória
bool coletorIO(StatusProcesso &medicao);      // preenche métricas de I/O
bool coletorNetwork(StatusProcesso &medicao); // preenche métricas de rede

void overheadMonitoramento(); // mede o overhead do monitoramento em si
void cargaExecutar();         // executa carga de teste para medir métricas
void limitacaoIO();           // executa teste de limitação de I/O

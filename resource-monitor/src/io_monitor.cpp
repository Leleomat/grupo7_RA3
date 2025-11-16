#include <iostream>
#include <bits/stdc++.h>
#include <chrono>
#include <thread>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fstream>
#include <string>
#include <sstream>
#include <cerrno>
#include <filesystem>
#include <vector>
#include <regex>
#include <unordered_map>
#include <system_error>
#include <cctype>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits>
#include "monitor.h"

bool coletorIO(StatusProcesso &medicao) {
    int pid = medicao.PID; // pega o PID do processo monitorado

    //Verifica se o processo existe
    if (!processoExiste(pid)) { 
        std::cerr << "Erro: processo " << pid << " inexistente.\n";
        return false; // retorna false se processo não existe
    }

    //Verifica se temos permissão para acessar arquivos do processo
    if (!temPermissao(pid)) {
        std::cerr << "Erro: sem permissão para acessar processo " << pid << ".\n";
        return false; // retorna false se não há permissão
    }

    std::string pathIO = "/proc/" + std::to_string(pid) + "/io"; //caminho do arquivo de I/O do proc
    std::ifstream arquivoIO(pathIO); //tenta abrir o arquivo
    //Checa se conseguimos abrir o arquivo
    if (!arquivoIO.is_open()) {
        std::cerr << "Erro: não foi possível abrir " << pathIO << "\n";
        std::cerr << "O processo encerrou entre verificações ou não possui permissões para o arquivo\n";
        return false; // retorna false se não abriu
    }

    std::string linha;
    while (std::getline(arquivoIO, linha)) { //lê linha por linha
        if (linha.rfind("read_bytes:", 0) == 0) // começa com read_bytes
            sscanf(linha.c_str(), "read_bytes: %lu", &medicao.bytesLidos); // atualiza bytes lidos
        else if (linha.rfind("write_bytes:", 0) == 0) // começa com write_bytes
            sscanf(linha.c_str(), "write_bytes: %lu", &medicao.bytesEscritos); // atualiza bytes escritos
        else if (linha.rfind("syscr:", 0) == 0) // syscall de leitura
            sscanf(linha.c_str(), "syscr: %lu", &medicao.syscallLeitura);
        else if (linha.rfind("syscw:", 0) == 0) // syscall de escrita
            sscanf(linha.c_str(), "syscw: %lu", &medicao.syscallEscrita);
        else if (linha.rfind("rchar:", 0) == 0) // bytes lidos pelo processo (aplicação)
            sscanf(linha.c_str(), "rchar: %lu", &medicao.rchar);
        else if (linha.rfind("wchar:", 0) == 0) // bytes escritos pelo processo (aplicação)
            sscanf(linha.c_str(), "wchar: %lu", &medicao.wchar);
    }

    return true; // retorna true se tudo ocorreu sem erro
}

bool coletorNetwork(StatusProcesso &status) { // entrada: recebe estrutura onde vai gravar métricas
    const int pid = status.PID; // PID do processo alvo
    const std::string caminhoDiretorioFd = "/proc/" + std::to_string(pid) + "/fd"; // path para os file descriptors do processo

    status.bytesRxfila = 0; // zera contador de bytes em fila de recepção
    status.bytesTxfila = 0; // zera contador de bytes em fila de transmissão
    status.conexoesAtivas = 0; // zera contador de sockets encontrados

    std::vector<unsigned long> listaInodesSockets; // vetor para armazenar inodes de sockets do processo
    if (std::filesystem::exists(caminhoDiretorioFd)) { // verifica existência do diretório /proc/<pid>/fd
        for (auto &entradaFd : std::filesystem::directory_iterator(caminhoDiretorioFd, std::filesystem::directory_options::skip_permission_denied)) { // itera FDs ignorando erros de permissão
            std::error_code codigoErro; // recebe erro de read_symlink sem lançar exceção
            auto linkSimbolico = std::filesystem::read_symlink(entradaFd.path(), codigoErro); // lê o symlink do FD (ex.: "socket:[12345]" ou "pipe:[5678]")
            if (codigoErro) continue; // se falhou ao ler o link, pula este FD
            std::string alvoLink = linkSimbolico.string(); // converte o path do symlink para string

            if (alvoLink.rfind("socket:[", 0) == 0) { // filtra apenas entradas que começam com "socket:["
                auto posAbertura = alvoLink.find('['); // posição do '['
                auto posFechamento = alvoLink.find(']'); // posição do ']'
                if (posAbertura != std::string::npos && posFechamento != std::string::npos && posFechamento > posAbertura+1) { // garante formato válido com número entre colchetes
                    try {
                        unsigned long numeroInode = std::stoul(alvoLink.substr(posAbertura+1, posFechamento-posAbertura-1), nullptr, 10); // extrai número do inode do texto
                        listaInodesSockets.push_back(numeroInode); // adiciona inode à lista
                    } catch (...) { continue; } // se conversão falhar, ignora este FD
                }
            }
        }
    }

    status.conexoesAtivas = static_cast<unsigned int>(listaInodesSockets.size()); // grava quantos sockets foram detectados no processo
    if (listaInodesSockets.empty()) return true; // se não há sockets, retorna rapidamente

    std::unordered_map<unsigned long, std::pair<unsigned long, unsigned long>> tabelaInodeParaFilas; // mapa: inode -> (rxQueue, txQueue)
    auto lerTabelaRede = [&](const std::string &caminhoTabela) { // lambda que faz parse de um /proc/net/*
        std::ifstream arquivoRede(caminhoTabela); // abre arquivo de tabela de sockets (tcp/udp)
        if (!arquivoRede.is_open()) return; // se não abrir, sai da lambda
        std::string linhaTabela;
        std::getline(arquivoRede, linhaTabela); // descarta cabeçalho (primeira linha)

        while (std::getline(arquivoRede, linhaTabela)) { // itera linhas restantes, cada uma descreve um socket
            if (linhaTabela.empty()) continue; // pula linhas vazias

            std::istringstream streamTokens(linhaTabela); // stream para tokenizar a linha por whitespace
            std::vector<std::string> listaTokens; // vetor temporário com tokens da linha
            std::string token;
            while (streamTokens >> token) listaTokens.push_back(token); // enche listaTokens com campos da linha
            if (listaTokens.empty()) continue; // se não houver tokens, pula

            std::string campoTxRxHex; // vai receber token no formato hex:hex (tx:rx)
            for (const auto &t : listaTokens) { // procura token que contenha ':' e seja hex:hex
                auto pos = t.find(':'); // posição do ':'
                if (pos == std::string::npos) continue; // não é o token TX:RX

                std::string parteEsquerda = t.substr(0, pos); // parte antes dos dois pontos (tx em hex)
                std::string parteDireita = t.substr(pos+1); // parte depois (rx em hex)
                if (parteEsquerda.empty() || parteDireita.empty()) continue; // precisa ter ambos os lados

                bool esquerdaHex = true, direitaHex = true; // flags de validação hex
                for (char c : parteEsquerda) if (!std::isxdigit((unsigned char)c)) { esquerdaHex = false; break; } // valida caracteres hex à esquerda
                for (char c : parteDireita) if (!std::isxdigit((unsigned char)c)) { direitaHex = false; break; } // valida caracteres hex à direita

                if (esquerdaHex && direitaHex) { campoTxRxHex = t; break; } // encontrou token válido TX:RX
            }
            if (campoTxRxHex.empty()) continue; // se não achou TX:RX, pula linha

            unsigned long valorTxFila = 0, valorRxFila = 0; // placeholders para valores convertidos
            try {
                auto separador = campoTxRxHex.find(':'); // separa hex tx e hex rx
                std::string hexTx = campoTxRxHex.substr(0, separador); // hex TX
                std::string hexRx = campoTxRxHex.substr(separador + 1); // hex RX
                valorTxFila = std::stoul(hexTx, nullptr, 16); // converte TX hex -> inteiro decimal
                valorRxFila = std::stoul(hexRx, nullptr, 16); // converte RX hex -> inteiro decimal
            } catch (...) { continue; } // em qualquer erro de conversão, pula linha

            unsigned long inodeLinha = 0; // vai receber o inode extraído da linha
            for (auto it = listaTokens.rbegin(); it != listaTokens.rend(); ++it) { // percorre tokens de trás pra frente procurando o inode numérico
                const std::string &tokenAtual = *it;
                if (tokenAtual.empty()) continue; // pula token vazio

                bool somenteDigitos = true;
                for (char c : tokenAtual) if (!std::isdigit((unsigned char)c)) { somenteDigitos = false; break; } // verifica se token é só dígitos

                if (somenteDigitos) {
                    try { inodeLinha = std::stoul(tokenAtual, nullptr, 10); } catch (...) { inodeLinha = 0; } // tenta converter para inteiro
                    if (inodeLinha != 0) break; // se converteu e não é zero, usa como inode
                }
            }

            if (inodeLinha == 0) continue; // sem inode válido, pula linha
            tabelaInodeParaFilas[inodeLinha] = {valorRxFila, valorTxFila}; // registra rx/tx para o inode
        }
    };

    lerTabelaRede("/proc/net/tcp"); // popula tabela com entradas TCP IPv4
    lerTabelaRede("/proc/net/tcp6"); // popula tabela com entradas TCP IPv6
    lerTabelaRede("/proc/net/udp"); // popula tabela com entradas UDP IPv4
    lerTabelaRede("/proc/net/udp6"); // popula tabela com entradas UDP IPv6

    for (unsigned long inodeAtual : listaInodesSockets) { // para cada inode que pertence ao processo
        auto it = tabelaInodeParaFilas.find(inodeAtual); // busca estatísticas desse inode na tabela de rede
        if (it != tabelaInodeParaFilas.end()) { // se encontrado (é socket de rede)
            status.bytesRxfila += it->second.first; // acumula bytes em fila de recepção
            status.bytesTxfila += it->second.second; // acumula bytes em fila de transmissão
        }
    }

    return true; // coleta concluída com sucesso
}

void limitacaoIO() {
    using namespace std::chrono; // usar tipos de tempo sem qualificar

    //Ler dados do usuário
    long long limiteBytesPorSegundo;                 // limite combinado (read+write) em B/s
    std::string caminhoArquivoFonte, caminhoArquivoDestino; // caminhos de fonte e destino
    size_t tamanhoBlocoBytes;                        // tamanho do buffer por iteração (bytes)
    unsigned duracaoSegundos;                        // duração fixa do experimento (0 = até EOF)

    std::cout << "Experimento 5 - Limitação de I/O (limite combinado read+write)\n"; // título

    std::cout << "Informe limite (B/s) (0 = sem limite): "; // pede limite
    std::cin >> limiteBytesPorSegundo;                   // lê limite

    std::cout << "Caminho do arquivo fonte (vazio -> /tmp/arquivo_grande_exp_io.bin): "; // pede fonte
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // limpa buffer do stdin
    std::getline(std::cin, caminhoArquivoFonte);        // lê linha com caminho da fonte
    if (caminhoArquivoFonte.empty())                    // se usuário não forneceu
        caminhoArquivoFonte = "/tmp/arquivo_grande_exp_io.bin"; // usa default

    std::cout << "Caminho do arquivo destino (será sobrescrito): "; // pede destino
    std::getline(std::cin, caminhoArquivoDestino);     // lê linha com caminho do destino
    if (caminhoArquivoDestino.empty())                 // se vazio
        caminhoArquivoDestino = "/tmp/saida_exp_io.bin"; // usa default

    std::cout << "Tamanho do bloco (bytes): ";          // pede tamanho do bloco
    std::cin >> tamanhoBlocoBytes;                      // lê tamanho do bloco

    std::cout << "Duração (segundos, 0 = até EOF): ";   // pede duração
    std::cin >> duracaoSegundos;                        // lê duração

    // Cria arquivo fonte caso não exista
    if (!std::filesystem::exists(caminhoArquivoFonte)) { // verifica existência
        const size_t tamanhoArquivoCriar = 5ULL * 1024ULL * 1024ULL; // MB
        std::cerr << "Arquivo fonte não existe. Criando arquivo de 5 MB...\n"; // log

        std::ofstream arquivoCriado(caminhoArquivoFonte, std::ios::binary); // abre para escrita binária
        if (!arquivoCriado) {                             // checa falha de abertura
            std::cerr << "Falha ao criar arquivo fonte.\n"; // log de erro
            return;                                       // sai da função
        }

        std::vector<char> blocoZeros(1024, 0);            // bloco de 1 KiB preenchido com zeros
        size_t bytesEscritos = 0;                         // contador de bytes escritos

        while (bytesEscritos < tamanhoArquivoCriar) {     // escreve até completar 100 MiB
            size_t quantidade = std::min(blocoZeros.size(), tamanhoArquivoCriar - bytesEscritos); // quanto escrever agora
            arquivoCriado.write(blocoZeros.data(), quantidade); // escreve bytes no arquivo
            bytesEscritos += quantidade;                   // atualiza contador
        }

        arquivoCriado.close();                            // fecha o arquivo criado
    }

    // tenta obter tamanho total do arquivo fonte para estimar impacto
    uint64_t tamanhoArquivoFonteBytes = 0;                // variável para guardar tamanho em bytes
    struct stat informacoesStat;                          // struct para stat()
    bool temTamanhoArquivo = (stat(caminhoArquivoFonte.c_str(), &informacoesStat) == 0); // tenta stat
    if (temTamanhoArquivo)                                // se stat ok
        tamanhoArquivoFonteBytes = static_cast<uint64_t>(informacoesStat.st_size); // guarda tamanho

    // Cria pipe para comunicação pai e filho
    int descritoresPipe[2];                               // descritores do pipe [0]=leitura, [1]=escrita
    if (pipe(descritoresPipe) == -1) {                    // cria pipe
        perror("pipe");                                   // erro se falhar
        return;                                           // sai
    }

    pid_t idFilho = fork();                               // cria processo filho
    if (idFilho < 0) {                                    // erro no fork
        perror("fork");                                   // mostra erro
        close(descritoresPipe[0]);                        // limpa pipe
        close(descritoresPipe[1]);
        return;                                           // sai
    }

    /* ==========================================
                 PROCESSO FILHO
       ========================================== */
    if (idFilho == 0) {                                   // bloco executado apenas no filho

        close(descritoresPipe[0]);                        // fecha extremidade de leitura no filho

        int fdFonte = open(caminhoArquivoFonte.c_str(), O_RDONLY); // abre fonte para leitura
        if (fdFonte < 0) {                                // erro ao abrir fonte
            perror("open fonte");                         // log erro
            _exit(2);                                     // sai do filho
        }

        int fdDestino = open(caminhoArquivoDestino.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644); // abre destino
        if (fdDestino < 0) {                             // erro ao abrir destino
            perror("open destino");                      // log erro
            close(fdFonte);                              // fecha fonte
            _exit(3);                                    // sai do filho
        }

        std::vector<char> buffer(tamanhoBlocoBytes ? tamanhoBlocoBytes : 65536); // buffer (default 64KiB)

        uint64_t bytesMovimentados = 0;                   // contador acumulado read+write
        uint64_t totalOperacoes = 0;                      // contador de iterações
        std::vector<double> latenciasMs;                  // vetor para latências amostrais
        latenciasMs.reserve(1024);                        // reserva para reduzir realocações

        double tokens = 0.0;                              // tokens disponíveis no bucket
        const double tokensPorNs = (limiteBytesPorSegundo > 0) ? static_cast<double>(limiteBytesPorSegundo) / 1e9 : 0.0; // refill por ns
        const double capacidadeBucket = (limiteBytesPorSegundo > 0) ? static_cast<double>(limiteBytesPorSegundo) : 0.0; // capacidade do bucket

        auto instanteAnterior = steady_clock::now();      // instante da última atualização de tokens
        auto instanteInicio = instanteAnterior;          // instante de início do experimento no filho
        auto proximoRelatorio = instanteAnterior + milliseconds(1000); // próximo envio de relatório (1s)

        while (true) {                                    // loop principal do workload
            auto agora = steady_clock::now();            // instante atual

            if (duracaoSegundos > 0) {                   // se há limite de tempo
                double decorrido = duration_cast<nanoseconds>(agora - instanteInicio).count() / 1e9; // calcula decorrido em s
                if (decorrido >= (double)duracaoSegundos) // se tempo estourou
                    break;                               // sai do loop
            }

            if (limiteBytesPorSegundo > 0) {              // se há limitação configurada
                auto deltaNs = duration_cast<nanoseconds>(agora - instanteAnterior).count(); // ns desde último refill
                if (deltaNs > 0) {                       // se passou tempo
                    tokens += tokensPorNs * static_cast<double>(deltaNs); // adiciona tokens proporcionais
                    if (tokens > capacidadeBucket)      // limita ao bucket
                        tokens = capacidadeBucket;
                    instanteAnterior = agora;           // atualiza instanteAnterior
                }
            }

            size_t maximoLeitura = buffer.size();        // assume que pode ler todo o buffer por padrão

            if (limiteBytesPorSegundo > 0) {              // se limitação ativa, ajusta granularidade
                if (tokens < 2.0) {                      // precisa ao menos 2 tokens (1 byte read + 1 byte write)
                    double faltamTokens = 2.0 - tokens;  // calcula tokens necessários
                    double tempoNecessarioNs = (tokensPorNs > 0.0) ? (faltamTokens / tokensPorNs) : 0.0; // ns necessários

                    if (tempoNecessarioNs > 0.0)         // se precisa esperar
                        std::this_thread::sleep_for(nanoseconds((long long)tempoNecessarioNs)); // dorme o mínimo necessário

                    continue;                           // volta ao topo após esperar/refill
                }

                size_t permitido = static_cast<size_t>(std::floor(tokens / 2.0)); // r tal que 2*r <= tokens
                if (permitido == 0) permitido = 1;  // segurança
                maximoLeitura = std::min(maximoLeitura, permitido); // limita read ao permitido
            }

            auto inicioIO = steady_clock::now();         // marca tempo antes do read
            ssize_t lidos = read(fdFonte, buffer.data(), maximoLeitura); // faz a leitura

            if (lidos < 0) {                             // erro de leitura
                perror("read");                          // log de erro
                break;                                   // interrompe loop do filho
            }
            if (lidos == 0) {                            // EOF detectado
                break;                                   // encerra loop
            }

            ssize_t totalEscrito = 0;                    // contador local de bytes escritos desta iteração
            while (totalEscrito < lidos) {               // loop até escrever tudo lido
                ssize_t w = write(fdDestino, buffer.data() + totalEscrito, lidos - totalEscrito); // escreve
                if (w <= 0) {                            // erro ao escrever
                    perror("write");                     // log erro
                    close(fdFonte);                      // fecha descritores
                    close(fdDestino);
                    _exit(4);                            // sai do filho com código
                }
                totalEscrito += w;                       // atualiza já escrito
            }

            auto fimIO = steady_clock::now();            // marca fim da operação read->write

            if (limiteBytesPorSegundo > 0) {              // desconta tokens conforme bytes lidos+escritos
                double gasto = (double)lidos * 2.0;      // 2 * r (read + write)
                tokens -= gasto;                         // consome tokens
                if (tokens < 0.0) tokens = 0.0;          // evita underflow
            }

            uint64_t latenciaNs = duration_cast<nanoseconds>(fimIO - inicioIO).count(); // lat em ns
            latenciasMs.push_back((double)latenciaNs / 1e6); // armazena latência em ms

            bytesMovimentados += (uint64_t)lidos * 2;       // contabiliza bytes movidos (read+write)
            totalOperacoes++;                               // incrementa contador de operações

            agora = steady_clock::now();                    // atualiza instante atual
            if (agora >= proximoRelatorio) {                // se passou 1 segundo desde último relatório
                double somaLat = 0.0;                       // soma latências do intervalo
                for (double v : latenciasMs) somaLat += v;
                double mediaLat = latenciasMs.empty() ? 0.0 : (somaLat / latenciasMs.size()); // média

                RelatorioFilho rel;                         // monta estrutura de relatório
                rel.bytesMovimentados = bytesMovimentados;  // total acumulado
                rel.operacoes = totalOperacoes;             // nº de iterações
                rel.latMediaMs = mediaLat;                  // média de lat do intervalo
                rel.timestampNs = (uint64_t) duration_cast<nanoseconds>(agora.time_since_epoch()).count(); // timestamp

                write(descritoresPipe[1], &rel, sizeof(rel)); // envia relatório para o pai
                latenciasMs.clear();                        // limpa amostras locais
                proximoRelatorio = agora + milliseconds(1000); // agenda próximo relatório (+1s)
            }
        }

        // relatório final antes de sair
        RelatorioFilho relFinal;                            // estrutura final
        relFinal.bytesMovimentados = bytesMovimentados;     // total final
        relFinal.operacoes = totalOperacoes;                // total de operações

        if (!latenciasMs.empty()) {                         // se restaram amostras
            double soma = 0.0;
            for (double v : latenciasMs) soma += v;
            relFinal.latMediaMs = soma / latenciasMs.size(); // média final
        } else {
            relFinal.latMediaMs = 0.0;                      // sem amostras -> 0
        }

        relFinal.timestampNs = (uint64_t) duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); // timestamp final

        write(descritoresPipe[1], &relFinal, sizeof(relFinal)); // envia relatório final

        close(fdFonte);                                     // fecha descritores antes de terminar
        close(fdDestino);
        close(descritoresPipe[1]);                           // fecha escrita do pipe
        _exit(0);                                           // encerra processo filho
    }

    /* ==========================================
                 PROCESSO PAI
       ========================================== */

    close(descritoresPipe[1]); // fecha extremidade de escrita no pai (pai só lê)

    StatusProcesso statusProcesso; // estrutura existente no seu projeto
    statusProcesso.PID = idFilho;  // guarda PID do filho para coletores

    std::vector<double> latenciasColetadas; // coleciona médias parciais vindas do filho
    latenciasColetadas.reserve(256);        // reserva para reduzir realocações

    uint64_t ultimoBytesMov = 0;             // último valor de bytes recebido do filho

    auto inicioPai = steady_clock::now();    // instante de início para o pai

    while (true) {
        fd_set conjuntoLeitura;               // conjunto para select
        FD_ZERO(&conjuntoLeitura);            // zera conjunto
        FD_SET(descritoresPipe[0], &conjuntoLeitura); // adiciona descritor do pipe

        struct timeval timeout;               // timeout para select
        timeout.tv_sec = 1;                   // 1 segundo
        timeout.tv_usec = 0;

        int pronto = select(descritoresPipe[0] + 1, &conjuntoLeitura, NULL, NULL, &timeout); // espera por dados

        if (pronto > 0 && FD_ISSET(descritoresPipe[0], &conjuntoLeitura)) { // se há dados no pipe
            RelatorioFilho rel;              // estrutura temporária
            ssize_t r = read(descritoresPipe[0], &rel, sizeof(rel)); // lê relatório
            if (r == sizeof(rel)) {          // se leitura completa
                if (rel.latMediaMs > 0.0)    // se relatório tem latência útil
                    latenciasColetadas.push_back(rel.latMediaMs); // armazena média parcial
                ultimoBytesMov = rel.bytesMovimentados; // atualiza bytes totais
            }
        }

        coletorIO(statusProcesso);            // coleta dados via /proc para validação (não usada na métrica principal)

        int status = 0;                       // status para waitpid
        pid_t r = waitpid(idFilho, &status, WNOHANG); // verifica se filho terminou sem bloquear
        if (r == idFilho) {                   // se filho finalizou           // marca flag
            break;                            // sai do loop
        }
    }

    // tenta ler eventuais relatórios finais deixados no pipe
    while (true) {
        RelatorioFilho rel;
        ssize_t r = read(descritoresPipe[0], &rel, sizeof(rel));
        if (r != sizeof(rel)) break;          // sai quando não há mais relatórios
        if (rel.latMediaMs > 0.0)             // se tem latência útil
            latenciasColetadas.push_back(rel.latMediaMs); // acumula
        ultimoBytesMov = rel.bytesMovimentados; // atualiza último total
    }

    close(descritoresPipe[0]);                // fecha leitura do pipe no pai

    auto fimPai = steady_clock::now();        // instante fim do experimento no pai
    double tempoTotalExecucao = duration_cast<duration<double>>(fimPai - inicioPai).count(); // tempo total em s

    double throughputReal = tempoTotalExecucao > 0.0 ? (double)ultimoBytesMov / tempoTotalExecucao : 0.0; // throughput em B/s

    double latenciaMediaMs = 0.0;             // média final de latências
    if (!latenciasColetadas.empty()) {
        double soma = 0.0;
        for (double v : latenciasColetadas) soma += v;
        latenciaMediaMs = soma / latenciasColetadas.size(); // calcula média
    }

    // cálculo do impacto no tempo total (se possível)
    bool impactoCalculavel = false;           // flag para saber se pode calcular impacto
    double tempoEsperado = 0.0;               // tempo teórico dado o limite
    double impactoPercentual = 0.0;           // impacto percentual

    if (temTamanhoArquivo && duracaoSegundos == 0 && limiteBytesPorSegundo > 0) { // condição para estimativa
        double bytesEstimados = (double)tamanhoArquivoFonteBytes * 2.0; // read + write
        tempoEsperado = bytesEstimados / (double)limiteBytesPorSegundo; // tempo teórico
        impactoCalculavel = true;             // marca calculável
        if (tempoEsperado > 0.0)
            impactoPercentual = ((tempoTotalExecucao / tempoEsperado) - 1.0) * 100.0; // percentual de impacto
    }

    // ---- impressão das métricas ----
    std::cout << "\n=== Métricas do Experimento de Limitação de I/O ===\n";
    if (limiteBytesPorSegundo > 0)
        std::cout << "Limite configurado (B/s): " << limiteBytesPorSegundo << "\n"; // imprime limite
    else
        std::cout << "Limite configurado: SEM_LIMITE\n"; // sem limite indicado

    std::cout << "Throughput medido (B/s): " << (unsigned long long)throughputReal << "\n"; // imprime throughput

    std::cout << "Latência média (ms/op): ";
    if (latenciasColetadas.empty())
        std::cout << "N/A\n";                       // sem latência disponível
    else
        std::cout << latenciaMediaMs << "\n";      // imprime lat média

    std::cout << "Tempo total de execução (s): " << tempoTotalExecucao << "\n"; // imprime tempo total

    if (impactoCalculavel) {                       // se cálculo disponível
        std::cout << "Tempo esperado pelo limite (s): " << tempoEsperado << "\n"; // imprime teórico
        std::cout << "Impacto no tempo total (%): " << impactoPercentual << "%\n"; // imprime impacto
    } else {
        std::cout << "Impacto no tempo total: N/A\n"; // não foi possível calcular
    }

    std::cout << "===================================================\n"; // rodapé
}

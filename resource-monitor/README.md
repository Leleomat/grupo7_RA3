# grupo7_RA3
Esta atividade propõe o desenvolvimento de um sistema de profiling e análise que permita monitorar, limitar e analisar o uso de recursos por processos e containers, explorando as primitivas do kernel Linux que tornam a containerização possível.


## Metodologia de Testes
## Responsável - Caliel Carvalho de Medeiros

### Teste de CPU

O teste de CPU verifica a precisão das medições de utilização de processamento. O programa executado realiza operações matemáticas contínuas utilizando um número conhecido de threads, cada uma ocupando aproximadamente um núcleo lógico. O profiler monitora o PID do processo enquanto o htop fornece valores de referência. A validação consiste em comparar o uso esperado com o uso reportado pelo kernel e o uso medido pelo profiler.

### Htop monitorando os cores do cpu
![alt text](docs/CPUhtop.png)

### Resource profiler monitorando CPU% por core e global
![alt text](docs/CPUprofiler.png)

Aqui é possível ver que o resource profiler consegue medir de forma precisa o uso da CPU e reportar métricas corretas,
neste caso o uso por core é 775% pois o computador utilizado possui 8 cores, de forma que 96,87% da CPU esteve ocupada com o programa de teste.



### Teste de Memória

O teste de memória valida a medição de alocação de memória RAM. O programa aloca um bloco de tamanho fixo no código e mantém essa alocação ativa até a finalização manual. Durante a execução, o profiler coleta métricas de RSS e memória virtual. As medições de referência são obtidas por /proc/[pid]/status, pmap e ps. A validação compara o valor alocado no código, os valores reportados pelo sistema e os valores capturados pelo profiler, verificando a coerência entre eles.

### Métricas de memória por proc/PID/status
![alt text](docs/memoriaProcStatus.png)

### Métricas de memória pelo resource profiler
![alt text](docs/memoriaProfiler.png)

Através dessa validação é possível visualizer que o programa consome ~~800Mb de RAM, sendo confirmado pelo acesso direto em proc/PID/status e pelo resource profiler, ambos confirmam que ele utiliza 822.528 kB de RAM física e 1.415.444 kb de memória virtual nesta instância de teste.


### Teste de I/O

O teste de I/O avalia a precisão na medição de bytes escritos e lidos. O programa executa operações contínuas de escrita em arquivo utilizando blocos de tamanho fixo, permitindo calcular exatamente o volume total de dados produzidos. O profiler monitora o PID enquanto os valores reais são obtidos em /proc/[pid]/io. A validação ocorre comparando os valores fornecidos pelo htop e os valores registrados pelo profiler. O programa de teste cria um arquivo .bin em tmp chamado teste_io, nele threads escrevem enquanto uma lê.


### Métricas de I/O pelo profiler
![alt text](docs/profilerIO.png)

### R/W do htop
![alt text](docs/htopIO.png)

Com base na análise das taxas pelo profiler e o htop, a taxa de escrita em disco total ficou constante em por volta de 76 kiB/s, enquanto htop e o profiler não detectaram leitura em disco devido ao cache, o profiler conseguiu capturar uma taxa de leitura lógica de 133.316 Mb/s. O comportamento é esperado pois as threads escrevem 100 bytes cada em ciclos de 5ms, sendo o que o número de threads é num_cores/2. Isso gera uma taxa de escrita constante, já a alta taxa de leitura vem do fato que o tamanho do arquivo aumenta a cada escrita, o que faz com seja o arquivo seja lido várias vezes em um segundo, enquanto cresce proporcionalmente. Os resultados são coerentes.

## Experimentos

### Experimento 1 - Overhead de Monitoramento - Caliel Carvalho de Medeiros

### Condições
O experimento consistiu em medir o overhead introduzido por um sistema de monitoramento de processos em execução no Linux. Um programa de carga de trabalho foi criado para consumir CPU de forma contínua por aproximadamente 5 segundos, realizando operações matemáticas moderadamente pesadas. A medição foi realizada em duas etapas: uma baseline sem monitoramento e execuções monitoradas com diferentes intervalos de amostragem (250 ms, 500 ms e 1000 ms).

### Execução
Durante a fase de baseline, o programa de carga foi executado isoladamente, sem coleta de métricas, para determinar o uso real de CPU e o tempo de execução natural da carga. O resultado obtido indicou um uso médio de CPU de 3.39366% e um tempo de execução de 5.00935 segundos.
Nas execuções monitoradas, o mesmo programa foi executado enquanto o profiler monitorava em intervalos definidos. Com intervalo de 250 ms, o uso médio de CPU monitorado foi de 3.83655%, representando um overhead de 0.44289% em relação à baseline. O tempo total de execução aumentou para 5.2285 segundos, enquanto a latência média da coleta de métricas foi de 0.897269 ms.
Com intervalo de 500 ms, o uso médio de CPU monitorado foi de 3.73543%, gerando um overhead de 0.341777%, com tempo de execução total de 5.05428 segundos e latência média de 0.907534 ms. Para intervalo de 1000 ms, o uso médio de CPU monitorado foi de 3.73837%, resultando em overhead de 0.344713%, tempo de execução de 5.01464 segundos e latência média de 0.839124 ms.

### Resultados
Os resultados indicam que, à medida que o intervalo de amostragem aumenta, a latência média da coleta tende a diminuir, enquanto o overhead de CPU não apresenta variação significativa, mantendo-se em torno de 0.34% a 0.44%. O tempo de execução monitorado permanece próximo ao tempo de baseline, confirmando que a coleta de métricas em intervalos regulares introduz impacto mínimo no desempenho da carga de trabalho, porém ainda introduz de forma que quanto menor a amostragem maior o overhead.

Testes subsequentes demonstraram que os resultados sofrem com ruído de medição.

### Experimento 5 - Limitação de I/O - Caliel Carvalho de Medeiros


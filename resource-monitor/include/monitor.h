#pragma once

struct StatusProcesso{
    int PID;
    //CPU
    double utime;   
    double stime;
    unsigned int threads;
    unsigned int contextSwitchfree;
    unsigned int contextSwitchforced;
    //Memória
    unsigned long vmSize; 
    unsigned long vmRss; 
    unsigned long vmSwap;
    unsigned long minfault;
    unsigned long mjrfault;
    //I/O
    unsigned long bytesLidos;
    unsigned long bytesEscritos;
    unsigned long syscallLeitura;
    unsigned long syscallEscrita;
    unsigned long rchar;
    unsigned long wchar;
};

bool processoExiste(int PID);

bool temPermissao(int PID);

bool coletorCPU(StatusProcesso &medicao);

bool coletorMemoria(StatusProcesso &medicao);

bool coletorIO(StatusProcesso &medicao);

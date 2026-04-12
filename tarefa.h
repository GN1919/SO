#ifndef tarefa_h
#define tarefa_h
#include <time.h> 
#include <stdio.h>
#include <stdlib.h>

struct tarefa{
char id[20];
time_t data_criacao;
int prioridade;
struct tarefa *prox;
};
typedef struct tarefa *Tarefa;
 struct lista_tarefas{
  Tarefa primeira_t;
};
typedef struct lista_tarefas * Lista_tarefas;

void inicializar(Lista_tarefas listas[6]);
void inserir(Lista_tarefas listas[6], int prioridade, char *id);
void listar(Lista_tarefas listas[6], int prioridade);
void remover(Lista_tarefas listas[6], char *id);


#endif

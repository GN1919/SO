#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tarefa.h"


void inicializar(Lista_tarefas listas[6]){
for(int i=0;i<6;i++){
listas[i] = (Lista_tarefas) malloc(sizeof(struct lista_tarefas));
listas[i]->primeira_t = NULL;
}
}
void inserir(Lista_tarefas listas[6],int prioridade, char *id){
Tarefa nova = (Tarefa) malloc(sizeof(struct tarefa));
nova->prioridade=prioridade;
strcpy(nova->id, id);
nova->data_criacao = time(NULL);
nova->prox = listas[prioridade]->primeira_t;
listas[prioridade]->primeira_t = nova;
}

void listar(Lista_tarefas listas[6], int prioridade){
for(int i=prioridade;i<=5;i++){
 printf("prioridade atual %d:\n",i);
Tarefa atual=listas[i]->primeira_t;
while(atual!=NULL){
printf("id %s\n", atual->id);
  printf("data: %s", ctime(&(atual->data_criacao)));
 atual=atual->prox;
}
}
}

void remover(Lista_tarefas listas[6], char *id){
 for(int i=0;i<=5;i++){
     Tarefa anterior = NULL; 
     Tarefa atual=listas[i]->primeira_t;
     while(atual!=NULL){
          if(strcmp(atual->id, id) == 0){
             if(anterior==NULL){
                  listas[i]->primeira_t = atual->prox;
                   free(atual);
               }else{
                   anterior->prox = atual->prox; 
                   free(atual);
               }
               return;
           }

            anterior = atual;
             atual = atual->prox;
   }
      
}
  printf("TAREFA INEXISTENTE\n");
}

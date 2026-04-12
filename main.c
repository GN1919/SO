#include "tarefa.h"
#include "commandlinereader.h"
#include <string.h>
int main(){
 Lista_tarefas listas[6];
inicializar(listas);

char *args[4];    /* array de 4 pos para guardar cada palavra do comando */
char buffer[100]; /* leitura o q o utilizador escreve */
while(1){
    int n = readLineArguments(args, 4, buffer, 100);
      if(n == -1) break;  /* n guarda o numero de argumentos lidos ou seja,ele guarda a 
linha completa no buffer e dps separa caa palavra guarda as palavras separadas no argv,com os limites do argv de 4 e buffer 100*/
  if(strcmp(args[0], "new") == 0){ /*apos receber o comando ele vai comparando o argumento da pos 0 q e o comando com
new,list ou complete para sabermos o q se deseja.a funcao atoi converte uma string em intprecisamod dela pq a funcao inserir espera int para id e priori   */
        inserir(listas, atoi(args[1]), args[2]);
    }else if(strcmp(args[0], "list") == 0){
        listar(listas, atoi(args[1]));
    }else if(strcmp(args[0], "complete") == 0){
        remover(listas, args[1]);
        
    }
}
 
return 0;
          
}


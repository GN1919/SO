#include <stdio.h>
#include <stdlib.h>
#include <sthread.h>

#define N_CAIXAS   2
#define N_CLIENTES 8
#define TEMPO_MIN  50
#define TEMPO_MAX  150

typedef struct no_cliente {
    int id;
    int tempo;
    int chamado;
    sthread_mon_t mon;
    struct no_cliente *prox;
} no_cliente_t;

typedef struct {
    sthread_mon_t mon;
    no_cliente_t *head;
    no_cliente_t *tail;
    int num;
    int espera;
} caixa_t;

static caixa_t caixas[N_CAIXAS];

static void fila_push(caixa_t *cx, no_cliente_t *no)
{
    no->prox = NULL;
    if (!cx->tail) { cx->head = cx->tail = no; }
    else { cx->tail->prox = no; cx->tail = no; }
    cx->num++;
}

static no_cliente_t *fila_pop(caixa_t *cx)
{
    if (!cx->head) return NULL;
    no_cliente_t *no = cx->head;
    cx->head = no->prox;
    if (!cx->head) cx->tail = NULL;
    cx->num--;
    no->prox = NULL;
    return no;
}

/* Atender: funcionário sinaliza cliente (sem preempção) e dorme */
static void Atender(int id_caixa, no_cliente_t *no)
{
    printf("[Caixa %d] Inicio atendimento cliente %d (%d ms)\n",
           id_caixa, no->id, no->tempo);

    sthread_monitor_enter(no->mon);
    no->chamado = 1;
    sthread_monitor_signal_np(no->mon);  /* sem preempção */
    sthread_monitor_exit(no->mon);

    sthread_sleep(no->tempo);
    printf("[DBG] Funcionario %d acordou do sleep\n", id_caixa);

    printf("[Caixa %d] Fim atendimento cliente %d\n", id_caixa, no->id);
}

/* SerAtendido: cliente espera ser chamado e depois dorme */
static void SerAtendido(no_cliente_t *no)
{
    sthread_monitor_enter(no->mon);
    while (!no->chamado)
        sthread_monitor_wait(no->mon);
    sthread_monitor_exit(no->mon);

    printf("[Cliente %d] A ser atendido (%d ms)\n", no->id, no->tempo);
    sthread_sleep(no->tempo);
    printf("[Cliente %d] Atendimento concluido\n", no->id);
    sthread_sleep(50);
    printf("[Cliente %d] Saiu do supermercado\n", no->id);
}

static no_cliente_t *EscolherFila(int id, int tempo)
{
    int c = 0;
    for (int i = 1; i < N_CAIXAS; i++)
        if (caixas[i].num < caixas[c].num) c = i;

    printf("[Cliente %d] Escolheu caixa %d (%d cliente(s))\n", id, c, caixas[c].num);

    no_cliente_t *no = malloc(sizeof(no_cliente_t));
    no->id      = id;
    no->tempo   = tempo;
    no->chamado = 0;
    no->mon     = sthread_monitor_init();
    no->prox    = NULL;

    sthread_monitor_enter(caixas[c].mon);
    fila_push(&caixas[c], no);
    if (caixas[c].espera)
        sthread_monitor_signal(caixas[c].mon);
    sthread_monitor_exit(caixas[c].mon);

    for (int j = 0; j < N_CAIXAS; j++) {
        if (j == c) continue;
        sthread_monitor_enter(caixas[j].mon);
        if (caixas[j].espera)
            sthread_monitor_signal(caixas[j].mon);
        sthread_monitor_exit(caixas[j].mon);
    }

    return no;
}

static no_cliente_t *ProximoCliente(int id_caixa)
{
    int outra = (id_caixa + 1) % N_CAIXAS;
    no_cliente_t *no;

    sthread_monitor_enter(caixas[id_caixa].mon);
    while (1) {
        if (caixas[id_caixa].num > 0) {
            no = fila_pop(&caixas[id_caixa]);
            sthread_monitor_exit(caixas[id_caixa].mon);
            return no;
        }
        sthread_monitor_enter(caixas[outra].mon);
        if (caixas[outra].num > 0) {
            no = fila_pop(&caixas[outra]);
            sthread_monitor_exit(caixas[outra].mon);
            printf("[Caixa %d] Atende cliente %d (da caixa %d)\n",
                   id_caixa, no->id, outra);
            sthread_monitor_exit(caixas[id_caixa].mon);
            return no;
        }
        sthread_monitor_exit(caixas[outra].mon);
        printf("[Caixa %d] Sem clientes (num=%d), a aguardar...\n", id_caixa, caixas[id_caixa].num);
        caixas[id_caixa].espera = 1;
        sthread_monitor_wait(caixas[id_caixa].mon);
        caixas[id_caixa].espera = 0;
        printf("[Caixa %d] Acordou! num=%d\n", id_caixa, caixas[id_caixa].num);
    }
}

void *tarefa_funcionario(void *arg)
{
    int id = (int)(long)arg;
    printf("[Caixa %d] Funcionario pronto.\n", id);
    while (1) {
        printf("[DBG] Funcionario %d a chamar ProximoCliente\n", id);
        no_cliente_t *no = ProximoCliente(id);
        Atender(id, no);
    }
    return NULL;
}

void *tarefa_cliente(void *arg)
{
    int id    = (int)(long)arg;
    int tempo = TEMPO_MIN + (rand() % (TEMPO_MAX - TEMPO_MIN));
    printf("[Cliente %d] Chegou (tempo: %d ms)\n", id, tempo);
    no_cliente_t *no = EscolherFila(id, tempo);
    SerAtendido(no);
    sthread_monitor_free(no->mon);
    free(no);
    return NULL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(42);
    sthread_init();

    for (int i = 0; i < N_CAIXAS; i++) {
        caixas[i].mon    = sthread_monitor_init();
        caixas[i].head   = NULL;
        caixas[i].tail   = NULL;
        caixas[i].num    = 0;
        caixas[i].espera = 0;
    }

    for (int i = 0; i < N_CAIXAS; i++)
        sthread_create(tarefa_funcionario, (void*)(long)i, 7);

    for (int i = 0; i < N_CLIENTES; i++)
        sthread_create(tarefa_cliente, (void*)(long)i, 7);

    sthread_exit(NULL);
    return 0;
}

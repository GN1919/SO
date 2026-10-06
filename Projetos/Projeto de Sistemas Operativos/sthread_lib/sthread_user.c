/* Simplethreads Instructional Thread Package
 * 
 * sthread_user.c - Implements the sthread API using user-level threads.
 *
 * Change Log:
 * 2002-04-15        rick
 *   - Initial version.
 * 2005-10-12        jccc
 *   - Added semaphores, deleted conditional variables
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <sthread.h>
#include <sthread_user.h>
#include <sthread_ctx.h>
#include <sthread_time_slice.h>
#include "queue.h"

#define MAX_PRIORITY 15
#define QUANTUM_BASE  5

struct _sthread {
  sthread_ctx_t *saved_ctx;
  sthread_start_func_t start_routine_ptr;
  long wake_time;
  int join_tid;
  void* join_ret;
  void* args;
  int tid;

  int prioridade_base;     /* prioridade inicial (imutável) */
  int prioridade_dinamica; /* prioridade actual (recalculada por época) */
  int quantum_restante;    /* quantum ainda disponível nesta época */
  int quantum_base;        /* quantum base (= QUANTUM_BASE) */
  int nice;                /* valor nice (0..10) */
};

typedef struct {
  queue_t *filas[MAX_PRIORITY];
} runqueue_t;


static runqueue_t *fila_ativas;    /* threads prontas para executar */
static runqueue_t *fila_expiradas; /* threads que esgotaram o quantum */
static queue_t    *dead_thr_list;
static queue_t    *sleep_thr_list;
static queue_t    *join_thr_list;
static queue_t    *zombie_thr_list;
static queue_t    *blocked_thr_list;
static struct _sthread *active_thr;  /* thread activa */
static struct _sthread *idle_thr;    /* thread idle (usada quando só há threads a dormir) */
static int tid_gen;                  /* gerador de tid's */

#define CLOCK_TICK 100
static long Clock;


/*********************************************************************/
/* Funções auxiliares internas                                        */
/*********************************************************************/

void sthread_user_free(struct _sthread *thread)
{
  sthread_free_ctx(thread->saved_ctx);
  free(thread);
}

runqueue_t *create_runqueue(void)
{
  runqueue_t *rq = malloc(sizeof(runqueue_t));
  for (int i = 0; i < MAX_PRIORITY; i++)
    rq->filas[i] = create_queue();
  return rq;
}

/*
 * recalcular_prioridade_quantum:
 *   - Tarefas com prioridade fixa (0..4): quantum e prioridade não mudam.
 *   - Tarefas com prioridade dinâmica (5..14):
 *       Quantum Inicial = Quantum Base + Quantum por Usar / 2
 *       Prioridade      = Prioridade Base - Quantum por Usar + Nice
 *   onde "Quantum por Usar" é o quantum que sobrou nesta época.
 */
void recalcular_prioridade_quantum(struct _sthread *thr)
{
  /* tarefas de prioridade fixa: nada muda */
  if (thr->prioridade_base >= 0 && thr->prioridade_base <= 4) {
    thr->prioridade_dinamica = thr->prioridade_base;
    thr->quantum_restante    = thr->quantum_base;
    return;
  }

  /* garantir que quantum não é negativo */
  if (thr->quantum_restante < 0)
    thr->quantum_restante = 0;

  int quantum_por_usar = thr->quantum_restante;

  /* fórmulas do enunciado */
  thr->quantum_restante    = thr->quantum_base + (quantum_por_usar / 2);
  thr->prioridade_dinamica = thr->prioridade_base - quantum_por_usar + thr->nice;

  /* limitar prioridade ao intervalo dinâmico [5, 14] */
  if (thr->prioridade_dinamica < 5)  thr->prioridade_dinamica = 5;
  if (thr->prioridade_dinamica > 14) thr->prioridade_dinamica = 14;
}

/*
 * remover_blocked: remove uma thread específica da lista global de bloqueadas.
 */
void remover_blocked(struct _sthread *thr)
{
  queue_t *tmp = create_queue();
  while (!queue_is_empty(blocked_thr_list)) {
    struct _sthread *t = queue_remove(blocked_thr_list);
    if (t != thr)
      queue_insert(tmp, t);
  }
  delete_queue(blocked_thr_list);
  blocked_thr_list = tmp;
}

/*
 * escolher_proxima_thread:
 *   Procura nas activas; se estiverem vazias e houver expiradas, troca as
 *   runqueues (nova época) e volta a procurar.
 */
struct _sthread *escolher_proxima_thread(void)
{
  /* procurar nas activas */
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_ativas->filas[i]))
      return queue_remove(fila_ativas->filas[i]);
  }

  /* verificar se há expiradas */
  int expiradas_vazias = 1;
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_expiradas->filas[i])) {
      expiradas_vazias = 0;
      break;
    }
  }

  if (expiradas_vazias)
    return NULL;

  /* troca de época: expiradas passam a activas */
  runqueue_t *tmp  = fila_ativas;
  fila_ativas      = fila_expiradas;
  fila_expiradas   = tmp;

  /* procurar novamente nas (novas) activas */
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_ativas->filas[i]))
      return queue_remove(fila_ativas->filas[i]);
  }

  return NULL;
}

/*
 * peek_proxima_thread:
 *   Devolve a próxima thread com maior prioridade nas activas SEM a remover.
 *   Usado para verificar preempção sem alterar o estado das filas.
 */
struct _sthread *peek_proxima_thread(void)
{
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_ativas->filas[i]))
      return fila_ativas->filas[i]->first->thread;
  }
  return NULL;
}

/*
 * remover_proxima_thread:
 *   Remove e devolve a próxima thread das activas (sem troca de época).
 */
struct _sthread *remover_proxima_thread(void)
{
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_ativas->filas[i]))
      return queue_remove(fila_ativas->filas[i]);
  }
  return NULL;
}

/*
 * check_preemption:
 *   Verifica se existe uma thread nas activas com prioridade superior
 *   (valor numérico menor) à thread actual. Se sim, faz preempção:
 *   a thread actual volta às activas e a nova thread executa imediatamente.
 *   Usa peek para não remover desnecessariamente.
 */
void check_preemption(void)
{
  splx(HIGH);

  struct _sthread *next = peek_proxima_thread();

  if (next == NULL) {
    splx(LOW);
    return;
  }

  if (next->prioridade_dinamica < active_thr->prioridade_dinamica) {
    struct _sthread *old = active_thr;

    /* remover agora que confirmámos a preempção */
    next = remover_proxima_thread();

    /* colocar a thread actual de volta nas activas */
    queue_insert(fila_ativas->filas[old->prioridade_dinamica], old);

    active_thr = next;
    sthread_switch(old->saved_ctx, active_thr->saved_ctx);
  }

  splx(LOW);
}


/*********************************************************************/
/* Part 1: Creating and Scheduling Threads                           */
/*********************************************************************/

void sthread_aux_start(void)
{
  splx(LOW);
  active_thr->start_routine_ptr(active_thr->args);
  sthread_user_exit((void*)0);
}

/* loop da thread idle: aguarda com interrupts activos até o dispatcher
   acordar uma thread a dormir e fazer switch para ela */
static void idle_loop(void)
{
  while (1) {
    splx(LOW);   /* garantir interrupts activos */
    splx(HIGH);  /* simular tick para o dispatcher correr */
    splx(LOW);
  }
}

void sthread_user_dispatcher(void);

void sthread_user_init(void)
{
  fila_ativas      = create_runqueue();
  fila_expiradas   = create_runqueue();
  dead_thr_list    = create_queue();
  sleep_thr_list   = create_queue();
  join_thr_list    = create_queue();
  zombie_thr_list  = create_queue();
  blocked_thr_list = create_queue();

  tid_gen = 1;

  /* criar thread principal (main) com prioridade dinâmica 5 */
  struct _sthread *main_thread = malloc(sizeof(struct _sthread));
  main_thread->start_routine_ptr = NULL;
  main_thread->args              = NULL;
  main_thread->saved_ctx         = sthread_new_blank_ctx();
  main_thread->wake_time         = 0;
  main_thread->join_tid          = 0;
  main_thread->join_ret          = NULL;
  main_thread->tid               = tid_gen++;
  main_thread->prioridade_base   = 5;
  main_thread->prioridade_dinamica = 5;
  main_thread->quantum_base      = QUANTUM_BASE;
  main_thread->quantum_restante  = QUANTUM_BASE;
  main_thread->nice              = 0;

  active_thr = main_thread;

  /* criar thread idle (nunca inserida nas filas, usada como destino
     quando não há threads executáveis mas há threads a dormir) */
  idle_thr = malloc(sizeof(struct _sthread));
  idle_thr->saved_ctx          = sthread_new_ctx((sthread_ctx_start_func_t)idle_loop);
  idle_thr->tid                = 0;  /* tid 0 = idle */
  idle_thr->prioridade_base    = 14;
  idle_thr->prioridade_dinamica = 14;
  idle_thr->quantum_base       = QUANTUM_BASE;
  idle_thr->quantum_restante   = QUANTUM_BASE;
  idle_thr->nice               = 0;
  idle_thr->start_routine_ptr  = NULL;
  idle_thr->args               = NULL;
  idle_thr->wake_time          = 0;
  idle_thr->join_tid           = 0;
  idle_thr->join_ret           = NULL;

  Clock = 1;
  sthread_time_slices_init(sthread_user_dispatcher, CLOCK_TICK);
}

/*
 * sthread_user_create:
 *   Cria uma nova thread com a prioridade inicial dada.
 *   Prioridades 0..4 são estáticas; 5..14 são dinâmicas.
 */
sthread_t sthread_user_create(sthread_start_func_t start_routine, void *arg, int priority)
{
  /* validar prioridade */
  if (priority < 0)  priority = 0;
  if (priority > 14) priority = 14;

  struct _sthread *new_thread = malloc(sizeof(struct _sthread));

  new_thread->args               = arg;
  new_thread->start_routine_ptr  = start_routine;
  new_thread->wake_time          = 0;
  new_thread->join_tid           = 0;
  new_thread->join_ret           = NULL;
  new_thread->saved_ctx          = sthread_new_ctx(sthread_aux_start);
  new_thread->prioridade_base    = priority;
  new_thread->prioridade_dinamica = priority;
  new_thread->quantum_base       = QUANTUM_BASE;
  new_thread->quantum_restante   = QUANTUM_BASE;
  new_thread->nice               = 0;

  splx(HIGH);

  new_thread->tid = tid_gen++;

  queue_insert(fila_ativas->filas[new_thread->prioridade_dinamica], new_thread);

  /* verificar se a nova thread tem maior prioridade que a actual */
  check_preemption();

  splx(LOW);

  return new_thread;
}

void sthread_user_exit(void *ret)
{
  splx(HIGH);

  int has_joiners = 0;

  /* desbloquear threads à espera no join */
  queue_t *tmp_queue = create_queue();
  while (!queue_is_empty(join_thr_list)) {
    struct _sthread *thread = queue_remove(join_thr_list);
    if (thread->join_tid == active_thr->tid) {
      thread->join_ret = ret;
      remover_blocked(thread);
      queue_insert(fila_ativas->filas[thread->prioridade_dinamica], thread);
      check_preemption();
      has_joiners = 1;
    } else {
      queue_insert(tmp_queue, thread);
    }
  }
  delete_queue(join_thr_list);
  join_thr_list = tmp_queue;

  if (!has_joiners)
    queue_insert(zombie_thr_list, active_thr);
  else
    queue_insert(dead_thr_list, active_thr);

  /* verificar se há threads executáveis (activas + expiradas) */
  int tem_executaveis = 0;
  for (int i = 0; i < MAX_PRIORITY; i++) {
    if (!queue_is_empty(fila_ativas->filas[i]))      { tem_executaveis = 1; break; }
  }
  if (!tem_executaveis) {
    for (int i = 0; i < MAX_PRIORITY; i++) {
      if (!queue_is_empty(fila_expiradas->filas[i])) { tem_executaveis = 1; break; }
    }
  }

  if (!tem_executaveis) {
    if (!queue_is_empty(blocked_thr_list)) {
      /* há threads bloqueadas: switch para idle_thr */
      /* o dispatcher vai encontrar threads nas activas e fazer switch */
      struct _sthread *old_thr = active_thr;
      active_thr = idle_thr;
      sthread_switch(old_thr->saved_ctx, idle_thr->saved_ctx);
      splx(LOW);
      return;
    } else {
      /* sem executáveis e sem bloqueadas: fim normal */
      sthread_user_free(active_thr);
      exit(0);
    }
  }

  struct _sthread *old_thr = active_thr;
  active_thr = escolher_proxima_thread();

  if (active_thr == NULL) {
    printf("ERRO: não há threads para executar\n");
    exit(1);
  }

  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  splx(LOW);
}

/*
 * sthread_user_dispatcher:
 *   Chamado a cada tick do relógio.
 *   Decrementa o quantum da thread activa. Se esgotou, move para expiradas
 *   e escolhe a próxima. Caso contrário, verifica apenas preempção.
 */
void sthread_user_dispatcher(void)
{
  Clock++;

  /* acordar threads adormecidas cujo tempo expirou */
  queue_t *tmp_queue = create_queue();
  while (!queue_is_empty(sleep_thr_list)) {
    struct _sthread *thread = queue_remove(sleep_thr_list);
    if (thread->wake_time <= Clock) {
      thread->wake_time = 0;
      remover_blocked(thread);
      queue_insert(fila_ativas->filas[thread->prioridade_dinamica], thread);
    } else {
      queue_insert(tmp_queue, thread);
    }
  }
  delete_queue(sleep_thr_list);
  sleep_thr_list = tmp_queue;

  /* se estamos na idle_thr, verificar se há threads para executar */
  if (active_thr == idle_thr) {
    /* verificar deadlock: bloqueadas que não estão no sleep */
    /* deadlock = sem activas, sem expiradas, sem sleeping, mas com bloqueadas */
    struct _sthread *next = escolher_proxima_thread();
    if (next != NULL) {
      active_thr = next;
      sthread_switch(idle_thr->saved_ctx, active_thr->saved_ctx);
    }
    /* sem executáveis e sem deadlock: só há threads a dormir, aguardar */
    return;
  }

  /* consumir quantum da thread activa normal */
  active_thr->quantum_restante--;

  if (active_thr->quantum_restante <= 0) {
    /* quantum esgotado: recalcular e mover para expiradas */
    recalcular_prioridade_quantum(active_thr);
    queue_insert(fila_expiradas->filas[active_thr->prioridade_dinamica], active_thr);

    struct _sthread *old_thr = active_thr;
    active_thr = escolher_proxima_thread();

    if (active_thr == NULL) {
      /* só há threads a dormir: ir para idle */
      active_thr = idle_thr;
    }

    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  } else {
    /* quantum não esgotado: verificar preempção */
    struct _sthread *next = peek_proxima_thread();

    if (next != NULL && next->prioridade_dinamica < active_thr->prioridade_dinamica) {
      struct _sthread *old = active_thr;
      next = remover_proxima_thread();
      queue_insert(fila_ativas->filas[old->prioridade_dinamica], old);
      active_thr = next;
      sthread_switch(old->saved_ctx, active_thr->saved_ctx);
    }
    /* senão, continua a executar a mesma thread */
  }
}

void sthread_user_yield(void)
{
  splx(HIGH);

  struct _sthread *old_thr = active_thr;

  /* colocar actual no fim da sua fila de prioridade */
  queue_insert(fila_ativas->filas[old_thr->prioridade_dinamica], old_thr);

  /* escolher próxima */
  struct _sthread *next = escolher_proxima_thread();

  if (next == NULL || next == old_thr) {
    active_thr = old_thr;
    splx(LOW);
    return;
  }

  active_thr = next;
  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  splx(LOW);
}

/*
 * sthread_user_nice:
 *   Modifica o valor nice da thread actual.
 *   Retorna a prioridade que a thread terá na próxima época SE esgotar
 *   o quantum (quantum_por_usar = 0 nesse caso).
 */
int sthread_user_nice(int nice)
{
  if (nice < 0 || nice > 10)
    return -1;

  active_thr->nice = nice;

  /* prioridade futura assumindo quantum esgotado (quantum_por_usar = 0) */
  int futura = active_thr->prioridade_base - 0 + nice;

  if (futura < 5)  futura = 5;
  if (futura > 14) futura = 14;

  return futura;
}


/*********************************************************************/
/* Part 2: Join and Sleep Primitives                                 */
/*********************************************************************/

int sthread_user_join(sthread_t thread, void **value_ptr)
{
  splx(HIGH);

  /* verificar se a thread já terminou (está na lista zombie) */
  int found = 0;
  queue_t *tmp_queue = create_queue();
  while (!queue_is_empty(zombie_thr_list)) {
    struct _sthread *zthread = queue_remove(zombie_thr_list);
    if (thread->tid == zthread->tid) {
      if (value_ptr != NULL)
        *value_ptr = zthread->join_ret;
      queue_insert(dead_thr_list, zthread);
      found = 1;
    } else {
      queue_insert(tmp_queue, zthread);
    }
  }
  delete_queue(zombie_thr_list);
  zombie_thr_list = tmp_queue;

  if (found) {
    splx(LOW);
    return 0;
  }

  /* verificar se a thread existe (activa, a dormir ou em join) */
  if (active_thr->tid == thread->tid)
    found = 1;

  for (int i = 0; i < MAX_PRIORITY && !found; i++) {
    queue_element_t *qe = fila_ativas->filas[i]->first;
    while (qe != NULL) {
      if (qe->thread->tid == thread->tid) { found = 1; break; }
      qe = qe->next;
    }
  }

  queue_element_t *qe = sleep_thr_list->first;
  while (!found && qe != NULL) {
    if (qe->thread->tid == thread->tid) found = 1;
    qe = qe->next;
  }

  qe = join_thr_list->first;
  while (!found && qe != NULL) {
    if (qe->thread->tid == thread->tid) found = 1;
    qe = qe->next;
  }

  if (!found) {
    splx(LOW);
    return -1;
  }

  /* bloquear à espera que a thread termine */
  active_thr->join_tid = thread->tid;

  struct _sthread *old_thr = active_thr;
  recalcular_prioridade_quantum(old_thr);
  queue_insert(join_thr_list, old_thr);
  queue_insert(blocked_thr_list, old_thr);

  active_thr = escolher_proxima_thread();

  if (active_thr == NULL) active_thr = idle_thr;

  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  if (value_ptr != NULL)
    *value_ptr = old_thr->join_ret;

  splx(LOW);
  return 0;
}

int sthread_user_sleep(int time)
{
  splx(HIGH);

  long num_ticks = 10 * time / CLOCK_TICK;
  if (num_ticks == 0) {
    splx(LOW);
    return 0;
  }

  active_thr->wake_time = Clock + num_ticks;

  recalcular_prioridade_quantum(active_thr);
  queue_insert(sleep_thr_list, active_thr);
  queue_insert(blocked_thr_list, active_thr);

  sthread_t old_thr = active_thr;
  active_thr = escolher_proxima_thread();

  if (active_thr == NULL) active_thr = idle_thr;

  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  splx(LOW);
  return 0;
}


/*********************************************************************/
/* Synchronization Primitives: Mutex                                  */
/*********************************************************************/

struct _sthread_mutex {
  lock_t   l;
  struct _sthread *thr;
  queue_t *queue;
};

sthread_mutex_t sthread_user_mutex_init()
{
  sthread_mutex_t lock;
  if (!(lock = malloc(sizeof(struct _sthread_mutex)))) {
    printf("Error in creating mutex\n");
    return 0;
  }
  lock->l     = 0;
  lock->thr   = NULL;
  lock->queue = create_queue();
  return lock;
}

void sthread_user_mutex_free(sthread_mutex_t lock)
{
  delete_queue(lock->queue);
  free(lock);
}

void sthread_user_mutex_lock(sthread_mutex_t lock)
{
  while (atomic_test_and_set(&(lock->l))) {}

  if (lock->thr == NULL) {
    lock->thr = active_thr;
    atomic_clear(&(lock->l));
  } else {
    recalcular_prioridade_quantum(active_thr);
    queue_insert(lock->queue, active_thr);
    queue_insert(blocked_thr_list, active_thr);
    atomic_clear(&(lock->l));

    splx(HIGH);
    struct _sthread *old_thr = active_thr;
    active_thr = escolher_proxima_thread();

    if (active_thr == NULL) {
      splx(LOW);
      while (1) {
        splx(HIGH);
        struct _sthread *next = escolher_proxima_thread();
        if (next != NULL) {
          active_thr = next;
          sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
          break;
        }
        if (queue_is_empty(sleep_thr_list) && !queue_is_empty(blocked_thr_list)) {
          printf("Deadlock detectado!\n");
          exit(1);
        }
        splx(LOW);
      }
    } else {
      sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
    }
    splx(LOW);
  }
}

void sthread_user_mutex_unlock(sthread_mutex_t lock)
{
  if (lock->thr != active_thr) {
    printf("unlock without lock!\n");
    return;
  }

  while (atomic_test_and_set(&(lock->l))) {}

  if (queue_is_empty(lock->queue)) {
    lock->thr = NULL;
  } else {
    lock->thr = queue_remove(lock->queue);
    remover_blocked(lock->thr);
    queue_insert(fila_ativas->filas[lock->thr->prioridade_dinamica], lock->thr);
    check_preemption();
  }

  atomic_clear(&(lock->l));
}


/*********************************************************************/
/* Synchronization Primitives: Monitor                               */
/*********************************************************************/

struct _sthread_mon {
  sthread_mutex_t mutex;
  queue_t        *queue;
};

sthread_mon_t sthread_user_monitor_init()
{
  sthread_mon_t mon;
  if (!(mon = malloc(sizeof(struct _sthread_mon)))) {
    printf("Error creating monitor\n");
    return 0;
  }
  mon->mutex = sthread_user_mutex_init();
  mon->queue = create_queue();
  return mon;
}

void sthread_user_monitor_free(sthread_mon_t mon)
{
  sthread_user_mutex_free(mon->mutex);
  delete_queue(mon->queue);
  free(mon);
}

void sthread_user_monitor_enter(sthread_mon_t mon)
{
  sthread_user_mutex_lock(mon->mutex);
}

void sthread_user_monitor_exit(sthread_mon_t mon)
{
  sthread_user_mutex_unlock(mon->mutex);
}

/*
 * sthread_user_monitor_wait:
 *   Bloqueia a thread actual na fila do monitor, libertando o mutex.
 *   Quando for desbloqueada, readquire o mutex antes de continuar.
 */
void sthread_user_monitor_wait(sthread_mon_t mon)
{
  if (mon->mutex->thr != active_thr) {
    printf("monitor wait called outside monitor\n");
    return;
  }

  struct _sthread *temp = active_thr;
  recalcular_prioridade_quantum(temp);

  queue_insert(mon->queue, temp);
  queue_insert(blocked_thr_list, temp);

  /* liberta o mutex (e potencialmente acorda threads à espera do mutex) */
  sthread_user_mutex_unlock(mon->mutex);

  splx(HIGH);
  struct _sthread *old_thr = active_thr;
  active_thr = escolher_proxima_thread();

  if (active_thr == NULL) active_thr = idle_thr;

  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

  /* ao acordar, readquirir o monitor */
  sthread_user_mutex_lock(mon->mutex);
  splx(LOW);
}

/*
 * sthread_user_monitor_signal:
 *   Desbloqueia uma thread da fila de espera do monitor (se existir).
 *   A thread desbloqueada é inserida nas activas; verifica preempção.
 */
void sthread_user_monitor_signal(sthread_mon_t mon)
{
  if (mon->mutex->thr != active_thr) {
    printf("monitor signal called outside monitor\n");
    return;
  }

  while (atomic_test_and_set(&(mon->mutex->l))) {}

  if (!queue_is_empty(mon->queue)) {
    struct _sthread *temp = queue_remove(mon->queue);
    remover_blocked(temp);
    queue_insert(fila_ativas->filas[temp->prioridade_dinamica], temp);
  }
  atomic_clear(&(mon->mutex->l));
  check_preemption();
}



void sthread_user_monitor_signal_np(sthread_mon_t mon)
{
  if (mon->mutex->thr != active_thr) {
    printf("monitor signal called outside monitor\n");
    return;
  }
  while (atomic_test_and_set(&(mon->mutex->l))) {}
  if (!queue_is_empty(mon->queue)) {
    struct _sthread *temp = queue_remove(mon->queue);
    remover_blocked(temp);
    queue_insert(fila_ativas->filas[temp->prioridade_dinamica], temp);
  }
  atomic_clear(&(mon->mutex->l));
  /* sem check_preemption: deixar o escalonador decidir normalmente */
}

/*********************************************************************/
/* Dummy monitors (pthreads não suporta monitores)                   */
/*********************************************************************/

sthread_mon_t sthread_dummy_monitor_init()
{
  printf("WARNING: pthreads do not include monitors!\n");
  return NULL;
}

void sthread_dummy_monitor_free(sthread_mon_t mon)
{
  printf("WARNING: pthreads do not include monitors!\n");
}

void sthread_dummy_monitor_enter(sthread_mon_t mon)
{
  printf("WARNING: pthreads do not include monitors!\n");
}

void sthread_dummy_monitor_exit(sthread_mon_t mon)
{
  printf("WARNING: pthreads do not include monitors!\n");
}

void sthread_dummy_monitor_wait(sthread_mon_t mon)
{
  printf("WARNING: pthreads do not include monitors!\n");
}

void sthread_dummy_monitor_signal(sthread_mon_t mon)
{
  printf("WARNING: pthreads do not include monitors!\n");
}


/*********************************************************************/
/* sthread_user_dump                                                  */
/*********************************************************************/

/*
 * sthread_user_dump:
 *   Imprime o estado actual das runqueues (activas e expiradas)
 *   e da lista de threads bloqueadas, no formato exigido pelo enunciado.
 */
void sthread_user_dump(void)
{
  printf("=== dump start ===\n");

  printf("active thread\n");
  if (active_thr != NULL) {
    printf("id: %d\n", active_thr->tid);
    printf("priority: %d\n", active_thr->prioridade_dinamica);
    printf("quantum: %d\n", active_thr->quantum_restante);
  }

  printf("active runqueue\n");
  for (int i = 0; i < MAX_PRIORITY; i++) {
    printf("[%d]", i);
    queue_element_t *qe = fila_ativas->filas[i]->first;
    while (qe != NULL) {
      struct _sthread *t = qe->thread;
      printf(" %d,%d", t->tid, t->quantum_restante);
      qe = qe->next;
    }
    printf("\n");
  }

  printf("expired runqueue\n");
  for (int i = 0; i < MAX_PRIORITY; i++) {
    printf("[%d]", i);
    queue_element_t *qe = fila_expiradas->filas[i]->first;
    while (qe != NULL) {
      struct _sthread *t = qe->thread;
      printf(" %d,%d", t->tid, t->quantum_restante);
      qe = qe->next;
    }
    printf("\n");
  }

  printf("blocked list\n");
  queue_element_t *qe = blocked_thr_list->first;
  while (qe != NULL) {
    struct _sthread *t = qe->thread;
    printf("%d,%d ", t->tid, t->quantum_restante);
    qe = qe->next;
  }
  printf("\n");

  printf("=== dump end ===\n");
}
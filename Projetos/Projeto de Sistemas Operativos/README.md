# Projeto de Sistemas Operativos — sthreads (threads em nível de utilizador)

Biblioteca de **threads implementadas em espaço de utilizador** (user-level threads) em C, com escalonador
por prioridades, `sleep`, `join`, **mutexes** e **monitores**. Está construída sobre o pacote didático
*Simplethreads* (Rick Cox, 2002). A implementação das user threads está em `sthread_lib/sthread_user.c`.

---

## 1. Estrutura do projeto

```
Projeto-Sistemas-Operativos/
├── Makefile                  # Makefile principal (compila sthread_lib e test-sthreads)
├── include/
│   ├── sthread.h             # API pública da biblioteca
│   └── config.h              # Configuração (i386, sem pthreads, time slices ligados)
├── sthread_lib/              # A biblioteca
│   ├── sthread.c             # Despacha cada chamada da API para a implementação activa
│   ├── sthread_user.c/.h     # Implementação das user threads (escalonador, join, sleep, mutex, monitor)
│   ├── sthread_ctx.c/.h      # Criação de contextos (pilha de 64 KB) e troca de contexto
│   ├── sthread_switch.S      # Troca de contexto em assembly i386 (pusha / troca de %esp / popa / ret)
│   ├── sthread_time_slice.c  # Timer (SIGALRM), splx() (liga/desliga interrupções), operações atómicas
│   ├── queue.c/.h            # Fila simples de threads (lista ligada)
│   ├── sthread_start.c / sthread_end.c   # Marcam o início/fim do código da biblioteca (proc_start / proc_end)
│   └── sthread_pthread.c/.h  # Implementação alternativa com pthreads (não é compilada por defeito)
└── test-sthreads/            # Programas de teste (cada .c tem o seu main)
```

---

## 2. Como funciona

### 2.1 Threads e troca de contexto
- Cada thread (`struct _sthread`) tem a sua própria **pilha de 64 KB** (`malloc`) e um contexto guardado (`sp`).
- A troca de thread é feita em `sthread_switch()`, que chama a rotina assembly `Xsthread_switch`:
  guarda os registos (`pusha`), guarda o `%esp` actual, carrega o `%esp` da outra thread e restaura os
  registos (`popa`) + `ret`.
- Uma thread nova começa em `sthread_aux_start()`, que activa as interrupções, chama a função do utilizador
  e, quando esta retorna, chama `sthread_exit`.
- A thread `main` é transformada na thread **tid 1** durante o `sthread_init()`. Existe também uma **thread idle**
  (tid 0), usada quando todas as threads estão bloqueadas ou a dormir.

### 2.2 Preempção por relógio
- `sthread_init()` arranca um timer (`setitimer` / `SIGALRM`). A cada tick é chamado o **dispatcher**
  (`sthread_user_dispatcher`), que acorda threads a dormir, desconta quantum à thread activa e decide se há troca.
- `splx(HIGH)` desliga as "interrupções" (ignora `SIGALRM`) e `splx(LOW)` volta a ligá-las. É assim que as
  secções críticas da biblioteca ficam protegidas.
- O handler só aceita o tick se o programa estiver a executar código entre `proc_start` e `proc_end`
  (código da biblioteca/aplicação). Se estiver dentro de código do sistema (ex.: `printf`), o tick é descartado.
- Carregar em **Ctrl+\\** (SIGQUIT) imprime quantos ticks foram aceites (*good*) e descartados (*dropped*).

### 2.3 Escalonador (prioridades + épocas)
- **15 níveis de prioridade (0 a 14)**; valor mais baixo = mais prioritária.
  - **0 a 4**: prioridade **fixa** (estática).
  - **5 a 14**: prioridade **dinâmica**.
- Existem duas runqueues, cada uma com 15 filas (uma por prioridade): **activas** e **expiradas**.
  O escalonador escolhe sempre a primeira thread da fila não-vazia de menor valor nas activas.
- Quando uma thread esgota o quantum, vai para as **expiradas** com prioridade/quantum recalculados.
  Quando as activas esvaziam, as duas runqueues **trocam de papel** (nova época).
- Para threads dinâmicas, ao fim de cada época:
```
  quantum_novo    = quantum_base + quantum_por_usar / 2        (quantum_base = 5)
  prioridade_nova = prioridade_base - quantum_por_usar + nice   (limitada a [5, 14])
```
- **Preempção:** se uma thread mais prioritária ficar pronta (por `create`, `unlock`, `signal`, `exit`/`join`...),
  a thread actual volta às activas e a mais prioritária passa a correr.
- `sthread_nice(n)` (0..10) altera o *nice* da thread actual e devolve a prioridade que ela terá na próxima época.

### 2.4 `sleep`, `join` e `exit`
- `sthread_sleep(ms)` bloqueia a thread numa lista de adormecidas; o dispatcher acorda-a quando `Clock >= wake_time`.
- `sthread_join(t, &ret)` bloqueia até `t` terminar e devolve o valor passado a `sthread_exit`. Se `t` já terminou
  (lista *zombie*), retorna logo; se `t` não existe, devolve `-1`.
- Quando a última thread termina e já não há bloqueadas, o programa faz `exit(0)`.

### 2.5 Mutex
- Mutex com *spinlock* interno (`atomic_test_and_set`, instrução `lock cmpxchg`), dono (`thr`) e fila de espera.
- `unlock` passa o mutex directamente à primeira thread da fila e verifica preempção.
- **Deteção de deadlock:** se uma thread se bloqueia num mutex e não há nenhuma thread executável nem a dormir,
  imprime `Deadlock detectado!` e termina com `exit(1)`.

### 2.6 Monitores
- Um monitor = mutex + fila de espera.
- `monitor_wait`: liberta o mutex, bloqueia-se na fila do monitor e, ao acordar, **readquire** o mutex.
- `monitor_signal`: acorda uma thread da fila e **verifica preempção**.
- `monitor_signal_np` ("no preemption"): acorda uma thread sem verificar preempção (usado no `supermercado.c`).

---

## 3. API (`include/sthread.h`)

| Função | Descrição |
|---|---|
| `sthread_init()` | Inicializa a biblioteca. Deve ser chamada uma vez, antes de qualquer outra. |
| `sthread_create(f, arg, prio)` | Cria uma thread (prioridade 0–14). Devolve o handle. |
| `sthread_exit(ret)` | Termina a thread actual. |
| `sthread_yield()` | Cede o CPU voluntariamente. |
| `sthread_sleep(ms)` | Suspende a thread actual. |
| `sthread_join(t, &ret)` | Espera que `t` termine (devolve `0`, ou `-1` se não existir). |
| `sthread_nice(n)` | Define o nice (0–10). |
| `sthread_dump()` | Imprime as runqueues activa/expirada e a lista de bloqueadas. |
| `sthread_mutex_init/free/lock/unlock` | Mutexes. |
| `sthread_monitor_init/free/enter/exit/wait/signal/signal_np` | Monitores. |

---

## 4. Requisitos

O código é **i386 (32 bits)**: usa assembly x86 de 32 bits e compila com `-m32`. São necessários:

- Linux (ou **WSL** no Windows). Não funciona em macOS com chip Apple (ARM).
- `gcc`, `make` e as bibliotecas de 32 bits.

```bash
# Ubuntu / Debian / WSL
sudo apt update
sudo apt install build-essential gcc-multilib

# Fedora
sudo dnf install gcc make glibc-devel.i686
```

> Erros como `bits/wordsize.h: No such file or directory` ou `bits/libc-header-start.h: No such file`
> indicam que falta o `gcc-multilib`.

---

## 5. Compilar

Na raiz do projeto:

```bash
make            # compila sthread_lib e os testes principais
make clean      # apaga .o, .a e os executáveis
```

O `make` principal compila `libsthread.a`, `libsthread_start.a`, `sthread_start.o` e os testes
`test-create`, `test-mutex`, `test-nice`, `01mutex01` … `04mutex04`, `05monitor01` … `09monitor05`.

Os restantes testes têm regra própria no Makefile e compilam-se à parte:

```bash
cd test-sthreads
make test-prioridade test-rr test-sleep test-join test-monitor test-deadlock
```

O `supermercado.c` e o `test-mutex1.c` não têm regra no Makefile e compilam-se manualmente
(a partir de `test-sthreads/`):

```bash
gcc -g -O0 -Wall -m32 -I../include ../sthread_lib/sthread_start.o \
    -o supermercado supermercado.c ../sthread_lib/libsthread.a
```

> O aviso `overriding recipe for target 'test-mutex'` é inofensivo (a regra está duplicada no Makefile).

---

## 6. Executar

Os executáveis ficam em `test-sthreads/`:

```bash
cd test-sthreads
./01mutex01
./07monitor03
./supermercado
```

### Testes disponíveis

| Programa | O que testa |
|---|---|
| `test-create` | Criação de uma thread + `yield` |
| `test-mutex` | Exclusão mútua (a main segura o lock, a thread filha tem de esperar) |
| `01mutex01` | Sequência `lock`/`unlock` numa só thread |
| `02mutex02` | `lock`/`unlock` com `yield` pelo meio |
| `03mutex03` | Duas threads a incrementar o mesmo contador com mutex |
| `04mutex04` | O `lock` bloqueia outra thread e o `unlock` liberta-a |
| `05monitor01` | `enter`/`exit` de monitor |
| `06monitor02` | Duas threads no mesmo monitor |
| `07monitor03` … `09monitor05` | `wait` numa thread e `signal` noutra |
| `test-nice` | 3 threads a alternar com `yield` (ciclo infinito: parar com Ctrl+C) |
| `test-rr` | Round-robin entre threads da mesma prioridade |
| `test-prioridade` | Threads de prioridade diferente (5 vs. 10) |
| `test-sleep` | `sthread_sleep` enquanto outra thread trabalha |
| `test-join` | `sthread_join` + valor de retorno |
| `test-monitor` | Produtor/consumidor simples com monitor |
| `test-deadlock` | Dois mutexes em ordem trocada: deve imprimir `Deadlock detectado!` |
| `supermercado` | Simulação: 2 caixas, 8 clientes, monitores + `signal_np` |

Os testes `01`–`09` terminam a imprimir `SUCESSO!` quando passam. Exemplo (`04mutex04`):

```
Testando sthread_mutex_*, impl: user
thread 1 do mutex
thread1 dentro do mutex
thread2 antes do mutex
thread1 depois do mutex
thread2 dentro do mutex
thread2 depois do mutex

SUCESSO!
```

---

## 7. Usar a biblioteca num programa

```c
#include <stdio.h>
#include <sthread.h>

void *tarefa(void *arg) {
    printf("Olá da thread %ld\n", (long)arg);
    return NULL;
}

int main(void) {
    sthread_init();                                    /* obrigatório, primeiro */
    sthread_t t = sthread_create(tarefa, (void*)1, 5); /* prioridade 5 */
    sthread_join(t, NULL);
    return 0;
}
```

Compilar a partir da raiz, depois de fazer `make`. A ordem importa: `sthread_start.o` primeiro e a biblioteca no fim.

```bash
gcc -g -O0 -m32 -I include sthread_lib/sthread_start.o \
    -o meu_prog meu_prog.c sthread_lib/libsthread.a
./meu_prog
```

---

## 8. Entrega (`make package`)

Depois de preencher no `Makefile` principal as variáveis `CAMPUS`, `CURSO`, `GRUPO`, `ALUNO1`, `ALUNO2` e `ALUNO3`:

```bash
make package
```

Gera `project-<CAMPUS>-<CURSO>-<GRUPO>-<ALUNO1>-<ALUNO2>-<ALUNO3>.tgz` com o projeto limpo.

---

## 9. Notas e limitações
	
- **Só i386.** A versão PowerPC não está implementada (`atomic_test_and_set` é um *placeholder*).
- **Pthreads não está activo:** `USE_PTHREADS` não está definido em `config.h` e `sthread_pthread.o` não faz parte do `Makefile`.
  Os monitores não existem na versão pthreads (só *dummies* que imprimem um aviso).
- Para recompilar de raiz: `make clean && make`.

## Créditos

Simplethreads © Rick Cox 2002, licença **GNU GPL** (ver `COPYING`), com código adaptado do GNU Pth 1.4.
Escalonador por prioridades, `sleep`/`join`, mutexes, monitores e testes desenvolvidos no âmbito do projeto de Sistemas Operativos.

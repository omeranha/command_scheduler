# ESP32 Scheduler

Firmware para ESP32 desenvolvido com ESP-IDF, responsável por controlar um LED através de uma interface CLI via UART, permitindo configurar o relógio do sistema, sincronizar a hora via NTP, configurar Wi-Fi e criar agendamentos persistentes em NVS.

O projeto utiliza FreeRTOS para separar o processamento da CLI, execução dos agendamentos, sincronização NTP e execução de efeitos no LED.

## Funcionalidades

* Controle do LED através de comandos via UART.
* Agendamentos diários ou associados a uma data específica.
* Ações:

  * `liga`
  * `desliga`
  * `pisca`
* Persistência dos agendamentos em NVS.
* Persistência das credenciais Wi-Fi em NVS.
* Configuração manual de data e hora.
* Sincronização da hora através de NTP.
* Interface CLI via UART a `115200 8N1`.
* Execução concorrente das tarefas utilizando FreeRTOS.
* Comunicação entre tasks através de queues.
* Proteção dos dados de agendamento através de mutex.

# Build

Abra um terminal configurado para o ESP-IDF, entre no diretório do projeto e configure o target correspondente ao seu chip:

```bash
idf.py set-target esp32
```

Depois compile:

```bash
idf.py build
```

---

# Flash

Conecte o ESP32 ao computador e execute:

```bash
idf.py flash
```

Para especificar manualmente a porta serial:

```bash
idf.py -p COM3 flash
```

No Linux, por exemplo:

```bash
idf.py -p /dev/ttyUSB0 flash
```

---

# Monitor serial

O firmware utiliza:

```text
Baud rate: 115200
Data bits: 8
Parity: None
Stop bits: 1
Flow control: Disabled
```

Para iniciar o monitor:

```bash
idf.py monitor
```

Ou especificando a porta:

```bash
idf.py -p COM3 monitor
```

---

# Configuração

## Wi-Fi

As credenciais são armazenadas na NVS utilizando o namespace:

```text
wifi
```

As chaves utilizadas são:

```text
ssid
password
```

Na primeira inicialização, caso não existam credenciais armazenadas, o firmware utiliza os valores definidos em:

```c
#define WIFI_DEFAULT_SSID "YOUR_WIFI_SSID"
#define WIFI_DEFAULT_PASSWORD "YOUR_WIFI_PASSWORD"
```

Depois das credenciais serem configuradas, elas são persistidas na flash.

## Timezone

O firmware configura:

```c
setenv("TZ", "UTC+3", 1);
tzset();
```

Embora pareça indicar UTC+3, essa é a convenção POSIX para timezone. Nesse formato, `UTC+3` corresponde a **UTC−3**, que é o horário utilizado pelo projeto.

O NTP fornece o horário de referência e o timezone é aplicado localmente pelo sistema.

---

# CLI

A comunicação com o firmware é feita através da UART.

Execute:

```text
help
```

para visualizar os comandos disponíveis.

---

## Comandos de horário

### `time get`

Exibe a data e hora atual do sistema.

```text
time get
```

Exemplo:

```text
Data atual: 2026-08-13 12:30:45
```

---

### `time set`

Define manualmente a hora mantendo a data atual:

```text
time set "12:30:00"
```

Também é possível definir data e hora simultaneamente:

```text
time set "2026-08-13 12:30:00"
```

O firmware valida:

* ano;
* mês;
* dia;
* hora;
* minuto;
* segundo;
* anos bissextos;
* quantidade de dias de cada mês.

---

### `ntp sync`

Inicia uma sincronização NTP em segundo plano:

```text
ntp sync
```

O firmware utiliza:

```text
pool.ntp.org
```

A sincronização possui timeout de 10 segundos.

Enquanto uma sincronização estiver em andamento, uma nova solicitação não é iniciada.

Exemplo:

```text
Sincronizacao NTP iniciada em segundo plano...
```

Após a conclusão:

```text
Hora sincronizada
```

ou:

```text
Falha na hora de sincronizar!
```

---

# Agendamentos

O firmware suporta até:

```text
5 agendamentos
```

definidos por:

```c
#define MAX_APPOINTMENTS 5
```

Cada agendamento contém:

* ID;
* data;
* horário;
* ação;
* parâmetros;
* estado.

Os agendamentos são persistidos na NVS e restaurados durante a inicialização.

---

## Ações disponíveis

### `liga`

Liga o LED:

```text
sched add 12:30:00 liga
```

### `desliga`

Desliga o LED:

```text
sched add 12:35:00 desliga
```

### `pisca`

Faz o LED piscar utilizando dois parâmetros:

```text
sched add 12:40:00 pisca 250 10
```

Onde:

```text
250 = intervalo entre mudanças de estado em ms
10  = duração em segundos
```

---

# Adicionar agendamento diário

Sintaxe:

```text
sched add HH:MM:SS <action> [params]
```

Exemplos:

```text
sched add 08:00:00 liga
```

```text
sched add 08:30:00 desliga
```

```text
sched add 12:00:00 pisca 250 10
```

Um agendamento com data `0` é tratado como diário.

---

# Adicionar agendamento para uma data específica

Também é possível informar uma data:

```text
sched add YYYY-MM-DD HH:MM:SS <action> [params]
```

Exemplos:

```text
sched add 2026-08-15 08:00:00 liga
```

```text
sched add 2026-08-15 08:30:00 desliga
```

```text
sched add 2026-08-15 12:00:00 pisca 250 10
```

A data é internamente representada no formato:

```text
YYYYMMDD
```

Por exemplo:

```text
2026-08-15
```

é armazenado como:

```text
20260815
```

---

# Agendamento relativo

O comando:

```text
sched in <delay_s> <action> [params]
```

cria um agendamento utilizando a hora atual como referência.

Exemplos:

```text
sched in 10 liga
```

Liga o LED aproximadamente 10 segundos depois.

```text
sched in 30 desliga
```

Desliga o LED aproximadamente 30 segundos depois.

Para piscar:

```text
sched in 10 pisca 250 5
```

Onde:

```text
10  = atraso em segundos
250 = intervalo do pisca em ms
5   = duração em segundos
```

O horário calculado é baseado na hora local atual e no número de segundos até a execução.

---

# Listar agendamentos

```text
sched list
```

Exemplo:

```text
ID  DATA         HORA      ACAO      PARAMETROS   ESTADO
--  ------------ --------  --------  ------------ -------
1   DIARIO       08:00:00  liga                   ativo
2   2026-08-15   12:30:00  pisca     250 10       ativo
```

---

# Remover agendamento

Utilize o ID:

```text
sched del <id>
```

Exemplo:

```text
sched del 2
```

O agendamento é removido da lista e a alteração é persistida na NVS.

---

# Remover todos os agendamentos

```text
sched clear
```

Esse comando remove os dados dos agendamentos da NVS e limpa a estrutura em RAM.

---

# Wi-Fi

## Alterar SSID

```text
wifi set ssid <name>
```

Exemplo:

```text
wifi set ssid MinhaRede
```

O valor é salvo imediatamente na NVS.

---

## Alterar senha

```text
wifi set password <password>
```

Exemplo:

```text
wifi set password MinhaSenha123
```

A senha também é salva imediatamente na NVS.

---

## Aplicar configuração

```text
wifi apply
```

O firmware aplica as credenciais atualmente carregadas e tenta reconectar ao access point.

---

# Arquitetura

O firmware é dividido principalmente entre módulos funcionais e tasks FreeRTOS.

## Módulos

### `wifi`

Responsável por:

* inicialização do driver Wi-Fi;
* criação da interface STA;
* carregamento das credenciais;
* armazenamento das credenciais em NVS;
* aplicação das credenciais;
* conexão e reconexão;
* controle do estado da conexão através de Event Group.

### `time_utils`

Responsável por operações relacionadas ao tempo:

* conversão de `HH:MM:SS`;
* conversão de segundos para horário;
* configuração manual do relógio;
* validação de datas;
* cálculo de anos bissextos;
* cálculo de dias do mês;
* criação de datas no formato `YYYYMMDD`.

### `appointments`

Responsável pela persistência dos agendamentos.

Os dados são armazenados em NVS como:

```text
appointments/list
appointments/count
```

O módulo também fornece a obtenção de IDs livres.

---

# Tasks FreeRTOS

O sistema utiliza três tasks principais.

## `mainTask`

Executa no core 0 com prioridade 1.

Responsabilidades:

* receber comandos pela UART;
* interpretar a CLI;
* executar comandos de horário;
* solicitar sincronização NTP;
* adicionar/remover/listar agendamentos;
* configurar Wi-Fi;
* enviar mensagens para o usuário.

Essa task funciona como o principal ponto de interação com o usuário.

---

## `appointmentTask`

Executa no core 1 com prioridade 1.

Responsabilidades:

* consultar o relógio do sistema;
* verificar os agendamentos ativos;
* comparar data e horário;
* executar os agendamentos quando o horário correspondente é atingido.

A task verifica os agendamentos aproximadamente uma vez por segundo.

---

## `led_blink_task`

Executa no core 1 com prioridade 2.

Responsabilidades:

* receber solicitações através de `blink_queue`;
* executar o efeito de piscar;
* controlar o GPIO do LED;
* manter a execução do efeito independente da CLI.

A utilização de uma queue evita que a execução do efeito de pisca precise permanecer dentro da lógica da CLI.

---

## `ntp_sync_task`

É criada sob demanda quando o comando:

```text
ntp sync
```

é executado.

Responsabilidades:

* verificar a conexão Wi-Fi;
* inicializar o SNTP;
* iniciar a sincronização;
* aguardar o resultado por até 10 segundos;
* enviar o resultado para `mainTask`;
* finalizar a própria task.

O resultado é enviado através de:

```c
ntp_done_queue
```

---

# Comunicação entre tasks

Foram utilizados diferentes mecanismos do FreeRTOS de acordo com o tipo de comunicação.

## Mutex

```c
appointments_mutex
```

Protege o acesso compartilhado aos:

```c
appointments
appointments_count
```

A `mainTask` modifica a lista durante operações da CLI enquanto a `appointmentTask` pode acessá-la simultaneamente.

A utilização do mutex estabelece uma região crítica lógica para essas operações.

---

## Queue de NTP

```c
ntp_done_queue
```

É utilizada para comunicar o resultado da sincronização NTP de volta para a `mainTask`.

Em vez de a task de NTP manipular diretamente a interface CLI, ela envia apenas o resultado:

```c
bool success
```

Isso mantém a responsabilidade de comunicação serial concentrada na `mainTask`.

---

## Queue de blink

```c
blink_queue
```

Transporta:

```c
blink_request_t
```

contendo:

* intervalo;
* duração;
* ID do agendamento.

A `appointmentTask` solicita o efeito e a `led_blink_task` executa a operação.

---

# Decisões de projeto

## Divisão das responsabilidades

A divisão em tasks foi feita para evitar que uma operação bloqueante impeça o restante do firmware de funcionar.

A `mainTask` concentra a interação com a UART, enquanto o processamento dos agendamentos acontece independentemente na `appointmentTask`.

O efeito `pisca` possui uma task própria porque ele pode durar vários segundos. Se fosse executado diretamente pela task de agendamento, o processamento dos demais agendamentos ficaria desnecessariamente acoplado à duração do efeito.

A sincronização NTP também ocorre em uma task separada, pois a espera pelo servidor pode levar vários segundos.

---

## Proteção contra condições de corrida

A estrutura de agendamentos é compartilhada por pelo menos duas tasks:

```text
mainTask
appointmentTask
```

Por isso foi criado um mutex:

```c
appointments_mutex
```

Operações de alteração da lista são realizadas dentro de uma região protegida:

```text
lock_appointments()
    ↓
modificação
    ↓
unlock_appointments()
```

Isso evita que uma task leia uma estrutura enquanto outra está modificando `appointments_count` ou os elementos da lista.

A comunicação entre tasks que não exige acesso compartilhado direto utiliza queues, reduzindo o acoplamento entre elas.

---

## Persistência

Os agendamentos e as credenciais Wi-Fi são armazenados em NVS.

A escolha da NVS permite que as configurações sobrevivam a:

* reinicializações;
* perda de alimentação;
* reset do firmware.

Os agendamentos são mantidos em RAM durante a operação normal e persistidos quando são modificados.

---

## Tempo

Os agendamentos não dependem diretamente de um timestamp Unix armazenado.

O horário de execução é representado como segundos desde meia-noite:

```text
HH * 3600 + MM * 60 + SS
```

e a data, quando necessária, é representada como:

```text
YYYYMMDD
```

Essa abordagem simplifica a comparação dos agendamentos com o horário local e evita armazenar um timestamp absoluto para cada agendamento.

---

# Limitações e pontos conhecidos

## Resolução dos agendamentos

A `appointmentTask` executa aproximadamente uma verificação por segundo.

Portanto, o sistema foi projetado para agendamentos com resolução de segundos, e não para aplicações que exigem precisão de milissegundos.

---

## Agendamento `sched in` próximo da meia-noite

O horário é calculado utilizando:

```text
(now + delay) % 86400
```

O agendamento criado possui `date = 0`, portanto é classificado como diário.

Isso significa que um comando como:

```text
sched in 120 liga
```

executado próximo da meia-noite pode resultar em um horário no dia seguinte, mas o agendamento continuará sendo classificado como diário.

---

## Agendamentos com data específica

Atualmente, a `appointmentTask` identifica que um agendamento possui data específica, mas o código não desativa explicitamente o agendamento após sua execução.

A variável:

```c
changed
```

é marcada, mas o estado do agendamento não é alterado.

---

## Concorrência durante operações de NVS

A persistência dos agendamentos é realizada enquanto o mutex de agendamentos permanece adquirido.

Isso garante que a estrutura não seja modificada simultaneamente durante a gravação, mas também significa que uma operação de flash pode manter outras operações aguardando o mutex.

Seria preferível copiar o estado para uma estrutura temporária e realizar a persistência fora da região crítica, mantendo o mutex bloqueado pelo menor tempo possível.

---

# O que poderia ser feito diferente

## 1. Módulo dedicado para a CLI

Atualmente a `mainTask` contém tanto a leitura da UART quanto praticamente toda a lógica dos comandos.

Separação entre:

```text
cli.c
scheduler.c
wifi.c
time_utils.c
```

A CLI seria responsável somente por:

```text
UART → parser → comando
```

enquanto os módulos executariam as operações.

Isso reduziria o tamanho da `mainTask` e facilitaria testes.

## 2. Parser da CLI

O parser atual utiliza principalmente `sscanf`.

Uma implementação futura poderia ter um parser estruturado, por exemplo:

```text
tokenize
    ↓
command
    ↓
subcommand
    ↓
arguments
    ↓
validation
```

Isso permitiria mensagens de erro mais precisas e evitaria aceitar comandos parcialmente válidos.

---
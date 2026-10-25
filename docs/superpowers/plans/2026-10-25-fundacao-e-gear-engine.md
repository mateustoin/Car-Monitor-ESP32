# Fundação (Etapa 0) + gear_engine (Etapa 1): plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para implementar este plano tarefa por tarefa. Os passos usam checkbox (`- [ ]`) para acompanhamento.

**Objetivo:** firmware ESP-IDF que conecta no ELM327 por Bluetooth clássico, lê os dados do carro e mostra-os no M5StickC Plus2, grava e reproduz viagens, e contém um `gear_engine` (marcha atual + conselho de troca econômico) testado no host.

**Arquitetura:** componentes ESP-IDF independentes em `components/`. A lógica pura (`elm327` parsing, `obd_poller` agendador, `vehicle_profile`, `gear_engine`, lógica de `ui`, decisão de `power_mgr`) não inclui FreeRTOS nem drivers e recebe o tempo como `now_ms`, por isso roda no alvo `linux` com Unity. O hardware fica atrás de `obd_transport` (vtable) e `board_api` (implementada por um BSP escolhido no Kconfig).

**Stack:** ESP-IDF v6.1 (C17), Bluedroid SPP, `esp_lcd` (ST7789) + LVGL 9 via `espressif/esp_lvgl_port`, `joltwallet/littlefs`, `esp_console`, NVS, Unity (alvo linux).

**Spec:** `docs/superpowers/specs/2026-10-25-assistente-marchas-design.md`

## Restrições globais

- Somente ESP-IDF **v6.1**. Ativar com `. ~/.espressif/tools/activate_idf_v6.1.sh`. Nada de Arduino.
- Alvo principal `esp32` (M5StickC Plus2, 8 MB de flash). O projeto também precisa compilar para `esp32c3`, com o SPP desligado e o BSP headless.
- Nenhum número específico do Sandero fora de `components/vehicle_profile/profiles/`.
- Componentes puros: sem `freertos/*`, `driver/*` ou `esp_lcd*` nos headers e fontes. Tempo sempre via parâmetro `uint32_t now_ms`.
- `transport_bt_spp` compila só com `CONFIG_SOC_BT_CLASSIC_SUPPORTED` + `CONFIG_BT_CLASSIC_ENABLED`.
- Identificadores em inglês, comentários e mensagens de UI em pt-br.
- Cores de fundo (spec): verde = ok `0x1B8A3A`, azul = suba `0x1565C0`, laranja = reduza `0xEF6C00`, cinza = sem dados/desconectado `0x424242` (os tons exatos são decisão minha).
- Parâmetros iniciais do perfil Sandero (spec): subir 2100 RPM (carga ≤ 20%) até 2800 RPM (carga ≥ 80%), RPM mínimo após subir 1500, reduzir abaixo de 1300 RPM com carga ≥ 60%, faixa alvo da redução 2000–2500 RPM, histerese da marcha 400 ms, amostra velha > 1000 ms.
- O bipe toca uma vez por evento, com intervalo mínimo de 3000 ms, e pode ser silenciado.
- Comando de testes no host (usado em várias tarefas, abreviado como **HOST**):
  `idf.py -C test/host set-target linux build && ./test/host/build/host_test.elf`. Se a v6.1 exigir, use `idf.py --preview -C test/host set-target linux`. Sucesso = `0 Failures`.

## Foco de revisão

1. Resposta do ELM chegando picada em vários `read()`, ou com prefixos `SEARCHING...` / `BUS INIT: ...OK`. O parser tem que acumular até o `>` e ignorar os prefixos. Testes nas Tarefas 2 e 3.
2. `rpm == 0`, `speed == 0` (parado, ponto morto rodando, ré): nada de divisão por zero nem NaN, a marcha fica 0 ("–") e não há conselho. Teste na Tarefa 6.
3. Amostra velha ou conexão caída: a marcha fica 0, o conselho fica NONE e `stale = true`. Nunca sobra a última sugestão congelada na tela. Teste na Tarefa 7.
4. Aprendizado contaminado por trocas e embreagem patinando, ou motorista que nunca usa a 5ª. Não pode surgir cluster falso, e o aparelho informa quantas marchas aprendeu (`learned_gears`). Teste na Tarefa 5.
5. Primeiro boot sem NVS, perfil genérico sem sementes, ou energia cortada durante a gravação. O sistema mostra o estado "aprendendo" sem travar, e o blob do NVS é gravado inteiro ou não é gravado. Testes nas Tarefas 7 e 9.

---

## Estrutura de arquivos

```
CMakeLists.txt  sdkconfig.defaults  sdkconfig.defaults.esp32  partitions.csv
main/            main.c, app_obd.c/.h (task OBD), app_ui.c/.h (task lógica+UI), idf_component.yml, Kconfig.projbuild
components/
  obd_transport/   include/obd_transport.h, obd_transport.c, transport_replay.c, transport_bt_spp.c, include/transport_replay.h, include/transport_bt_spp.h, Kconfig
  elm327/          include/elm327.h, elm327.c (sessão), elm327_parse.c (puro)
  obd_poller/      include/obd_poller.h, obd_poller.c
  vehicle_profile/ include/vehicle_profile.h, vehicle_profile.c, profiles/sandero_k7m.c, profiles/generic_manual.c
  gear_engine/     include/gear_engine.h, gear_learn.c/.h, gear_detect.c/.h, shift_advisor.c/.h, gear_engine.c
  ui/              include/ui.h, ui_layout.c, ui_colors.c, ui_alert.c (puros), ui_screen.c (LVGL)
  power_mgr/       include/power_mgr.h, power_policy.c (puro), power_mgr.c
  storage/         include/storage.h, storage.c (NVS)
  obd_recorder/    include/obd_recorder.h, rec_format.c (puro), obd_recorder.c (sinks + console)
  board_api/       include/board_api.h, Kconfig (choice da placa)
  bsp_m5stickc_plus2/  bsp.c
  bsp_headless/        bsp.c
test/host/       CMakeLists.txt, sdkconfig.defaults, main/{CMakeLists.txt, test_main.c, test_<componente>.c}
tools/replay/    app linux: roda uma gravação pelo elm327+poller+gear_engine e imprime um CSV
```

---

### Tarefa 1: Esqueleto do projeto e app de testes no host

**Arquivos:**
- Criar: `CMakeLists.txt`, `sdkconfig.defaults`, `sdkconfig.defaults.esp32`, `partitions.csv`, `main/main.c`, `main/CMakeLists.txt`, `.gitignore`
- Criar: `test/host/CMakeLists.txt`, `test/host/sdkconfig.defaults`, `test/host/main/CMakeLists.txt`, `test/host/main/test_main.c`

**Interfaces:**
- Produz: o padrão de testes. Cada `test/host/main/test_<x>.c` expõe `void run_<x>_tests(void);` (com `RUN_TEST` dentro), e `test_main.c` chama todos entre `UNITY_BEGIN()`/`UNITY_END()` e faz `exit(UNITY_END())`.

- [ ] **Passo 1:** Criar `test/host`. O projeto usa `set(EXTRA_COMPONENT_DIRS ../../components)`, `set(COMPONENTS main)` e `sdkconfig.defaults` com `CONFIG_IDF_TARGET="linux"` e `CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER=n`. `test_main.c` tem um `test_sanity` que faz `TEST_ASSERT_EQUAL(1, 1)`.
- [ ] **Passo 2:** Rodar **HOST**. Esperado: `1 Tests 0 Failures`.
- [ ] **Passo 3:** Criar o projeto raiz: `project(car_monitor)`. `sdkconfig.defaults` com `CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` e `CONFIG_FREERTOS_HZ=1000`. `sdkconfig.defaults.esp32` com BT: `CONFIG_BT_ENABLED`, `CONFIG_BT_BLUEDROID_ENABLED`, `CONFIG_BT_CLASSIC_ENABLED`, `CONFIG_BT_SPP_ENABLED`, `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY`. `partitions.csv`: `nvs` 0x6000, `phy_init` 0x1000, `factory` app 3M, `storage` data littlefs com o resto. `main.c` só faz `ESP_LOGI` de "car_monitor".
- [ ] **Passo 4:** `idf.py set-target esp32 build`. Esperado: build OK.
- [ ] **Passo 5:** Commit `chore: esqueleto ESP-IDF 6.1 e app de testes no host`.

---

### Tarefa 2: `obd_transport`, transporte replay e formato de gravação

**Arquivos:**
- Criar: `components/obd_transport/{CMakeLists.txt, Kconfig, include/obd_transport.h, include/transport_replay.h, obd_transport.c, transport_replay.c}`
- Criar: `components/obd_recorder/{CMakeLists.txt, include/obd_recorder.h, rec_format.c}` (só a parte pura nesta tarefa)
- Teste: `test/host/main/test_transport.c`

**Interfaces:**
- Produz (`obd_transport.h`):
```c
typedef struct obd_transport obd_transport_t;
typedef struct {
    esp_err_t (*connect)(obd_transport_t *t, uint32_t timeout_ms);
    esp_err_t (*write)(obd_transport_t *t, const char *data, size_t len);
    int  (*read)(obd_transport_t *t, char *buf, size_t cap, uint32_t timeout_ms); /* >0 bytes, 0 timeout, <0 erro */
    void (*disconnect)(obd_transport_t *t);
    bool (*is_connected)(obd_transport_t *t);
    void (*destroy)(obd_transport_t *t);
} obd_transport_ops_t;
struct obd_transport { const obd_transport_ops_t *ops; void *ctx; };
/* Envia cmd + "\r" e acumula até '>'. resp sai sem o '>' e sem espaços/CR nas pontas. */
esp_err_t obd_transport_command(obd_transport_t *t, const char *cmd, char *resp, size_t cap, uint32_t timeout_ms);
/* ESP_ERR_TIMEOUT sem '>' no prazo; ESP_ERR_INVALID_SIZE se estourar cap; ESP_ERR_INVALID_STATE desconectado */
```
- Produz (`transport_replay.h`): `esp_err_t transport_replay_create(const char *recording, size_t chunk, obd_transport_t **out);` e `uint32_t transport_replay_now_ms(obd_transport_t *t);` (timestamp do último registro entregue). `chunk` > 0 entrega a resposta em pedaços desse tamanho, para simular o SPP picado.
- Produz (`obd_recorder.h`, parte pura). Formato de linha: `t_ms\tCMD\tRESP\n`, em que os CRs internos de RESP viram `|`.
```c
int  rec_format_line(char *out, size_t cap, uint32_t t_ms, const char *cmd, const char *resp); /* bytes ou -1 */
bool rec_parse_line(const char *line, uint32_t *t_ms, char *cmd, size_t cmd_cap, char *resp, size_t resp_cap);
```
- Semântica do replay: a cada `write(cmd)`, procura a partir do cursor o próximo registro com o mesmo CMD e entrega `RESP` (com `|`→`\r`) + `"\r\r>"`. Se não achar, entrega `"NO DATA\r\r>"`.

- [ ] **Passo 1: testes que falham:**
  - `test_rec_roundtrip`: formata `(1234,"010C","SEARCHING...\r410C1AF8")` e o parse devolve os mesmos valores.
  - `test_command_reassembles_chunks`: replay com `chunk=3` e a gravação `"0\t010C\t410C1AF8\n"`. `obd_transport_command(t,"010C",...)` devolve `ESP_OK` e `resp == "410C1AF8"`.
  - `test_command_timeout`: transporte fake que nunca manda `>` → `ESP_ERR_TIMEOUT`.
  - `test_command_overflow`: `cap=4` → `ESP_ERR_INVALID_SIZE`.
  - `test_replay_unknown_cmd`: comando não gravado → `resp == "NO DATA"`.
  - `test_replay_clock`: depois de responder o registro `t=500`, `transport_replay_now_ms == 500`.
- [ ] **Passo 2:** **HOST** → os testes falham por falta de símbolos.
- [ ] **Passo 3:** Implementar. A `Kconfig` declara `OBD_TRANSPORT_BT_SPP` (bool, default y, `depends on SOC_BT_CLASSIC_SUPPORTED && BT_CLASSIC_ENABLED`), `OBD_ELM_BT_NAME` (string "OBDII") e `OBD_ELM_BT_PIN` (string "1234"). `transport_bt_spp.c` ainda não entra.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(obd_transport): interface genérica, replay e formato de gravação`.

---

### Tarefa 3: `elm327`

**Arquivos:**
- Criar: `components/elm327/{CMakeLists.txt, include/elm327.h, elm327_parse.c, elm327.c}`
- Teste: `test/host/main/test_elm327.c`

**Interfaces:**
- Consome: `obd_transport_command` (Tarefa 2), `transport_replay_create` nos testes.
- Produz:
```c
#define ELM_PID_RPM 0x0C
#define ELM_PID_SPEED 0x0D
#define ELM_PID_THROTTLE 0x11
#define ELM_PID_LOAD 0x04
#define ELM_PID_MAP 0x0B
#define ELM_PID_COOLANT 0x05
#define ELM_PID_IAT 0x0F
typedef void (*elm327_tap_t)(void *arg, const char *cmd, const char *resp);
typedef struct {
    obd_transport_t *t; uint32_t timeout_ms;
    char protocol[32];          /* resposta de ATDPN, ex. "A6" ou "A5" */
    uint32_t supported[3];      /* bitmasks 0100, 0120, 0140 */
    elm327_tap_t tap; void *tap_arg;
} elm327_t;
esp_err_t elm327_init(elm327_t *e, obd_transport_t *t, uint32_t timeout_ms);
bool      elm327_pid_supported(const elm327_t *e, uint8_t pid);
esp_err_t elm327_read_pid(elm327_t *e, uint8_t pid, float *value);   /* ESP_ERR_NOT_FOUND = NO DATA */
esp_err_t elm327_read_voltage(elm327_t *e, float *volts);            /* ATRV, ex. "12.6V" */
void      elm327_set_tap(elm327_t *e, elm327_tap_t tap, void *arg);  /* chamado a cada troca cmd/resp */
/* puros */
esp_err_t elm327_parse_mode01(const char *resp, uint8_t pid, uint8_t *data, size_t cap, size_t *len);
esp_err_t elm327_decode_pid(uint8_t pid, const uint8_t *data, size_t len, float *value);
```
- Sequência de `elm327_init`: `ATZ`, `ATE0`, `ATL0`, `ATS0`, `ATH0`, `ATSP0`, `0100` (com timeout longo, ≥ 10 s, porque dispara a busca de protocolo), `ATDPN`, depois `0120`/`0140` só se o bit do PID 0x20/0x40 estiver ligado.
- Fórmulas: 0C `(256A+B)/4`; 0D `A`; 11 e 04 `A*100/255`; 0B `A` kPa; 05 e 0F `A-40`. Bitmask: o bit 31 do 4º byte lido corresponde ao PID base+1.

- [ ] **Passo 1: testes que falham:**
  - `test_parse_plain`: `"410C1AF8"` → data `{0x1A,0xF8}`, e decode → `1726.0f`.
  - `test_parse_with_searching`: `"SEARCHING...\r410D3C"` → speed `60.0f`.
  - `test_parse_bus_init`: `"BUS INIT: ...OK\r410D3C"` → `60.0f`.
  - `test_parse_no_data`: `"NO DATA"`, `"?"`, `"STOPPED"`, `"CAN ERROR"`, `"UNABLE TO CONNECT"` → `ESP_ERR_NOT_FOUND` (os dois primeiros) ou `ESP_FAIL` (os demais).
  - `test_parse_multi_ecu`: `"7E8...\r410C0FA0\r410C0FA0"` com headers desligados → pega a primeira linha `41 0C`.
  - `test_decode_all`: 11 com `A=255` → `100.0f`; 05 com `A=130` → `90.0f`; 0B com `A=35` → `35.0f`.
  - `test_init_and_supported`: gravação replay com respostas a `ATZ`…`0100="4100BE1FA813"`, `ATDPN="A5"`. Esperado: `elm327_pid_supported(e,0x0C)` true, `(e,0x0B)` true, protocol `"A5"`, e nenhum `0120` enviado se o bit do PID 0x20 estiver desligado (o replay conta os comandos recebidos).
  - `test_read_voltage`: `"12.6V"` → `12.6f`.
  - `test_tap_called`: o tap recebe `("010C","410C1AF8")`.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar. `elm327_parse.c` não depende de transporte.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(elm327): inicialização, parsing e decodificação de PIDs`.

---

### Tarefa 4: `vehicle_profile`

**Arquivos:**
- Criar: `components/vehicle_profile/{CMakeLists.txt, include/vehicle_profile.h, vehicle_profile.c, profiles/sandero_k7m.c, profiles/generic_manual.c}`
- Teste: `test/host/main/test_vehicle_profile.c` (define também o perfil fictício `test_six_gear`, só nos testes)

**Interfaces:**
- Produz:
```c
#define VP_MAX_GEARS 8
typedef enum { VP_FUEL_FLEX, VP_FUEL_GASOLINE, VP_FUEL_ETHANOL, VP_FUEL_DIESEL } vp_fuel_t;
typedef struct {
    const char *id;               /* ≤ 12 chars: vira chave NVS "r_<id>" */
    const char *name;
    uint8_t gears; vp_fuel_t fuel;
    float seed_ratios[VP_MAX_GEARS];      /* km/h por 1000 RPM; 0 = sem semente */
    uint16_t idle_rpm, redline_rpm;
    uint16_t up_rpm_light, up_rpm_heavy;  /* limite de subida em função da carga */
    uint8_t  load_light_pct, load_heavy_pct;
    uint16_t min_rpm_after_upshift;
    uint16_t lug_rpm; uint8_t lug_load_pct;
    uint16_t target_band_low, target_band_high;
    float    ratio_tolerance;              /* fração, ex. 0.07 */
    uint16_t gear_hold_ms;
    uint16_t stale_ms;
} vehicle_profile_t;
size_t vp_count(void);
const vehicle_profile_t *vp_get(size_t i);
const vehicle_profile_t *vp_find(const char *id);   /* NULL se não existe */
const vehicle_profile_t *vp_default(void);          /* "sandero_k7m" */
bool vp_validate(const vehicle_profile_t *p);       /* gears 1..8, up_light<up_heavy, band_low<band_high, ... */
```
- `sandero_k7m`: 5 marchas, FLEX, sementes 0 (relações do JH3 não confirmadas, ver spec), idle 800, redline 6000, 2100/2800 com cargas 20/80, min após subir 1500, lug 1300 com carga 60, faixa 2000–2500, tolerância 0.07, hold 400, stale 1000.
- Relações aprendidas ficam associadas ao `id` do perfil. A associação por VIN fica para a Etapa 7 (decisão minha).
- `generic_manual`: os mesmos limiares, 5 marchas, GASOLINE, sem sementes. É uma decisão minha que o genérico use os mesmos números.

- [ ] **Passo 1: testes que falham:** `test_find_default` (`vp_default()->id == "sandero_k7m"`, `gears == 5`); `test_find_unknown` (NULL); `test_all_builtin_valid` (todo `vp_get(i)` passa em `vp_validate`); `test_validate_rejects` (perfil com `up_rpm_light > up_rpm_heavy` → false; `gears = 0` → false; `id` com 13 chars → false).
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar. A tabela de perfis é um array `const` em `vehicle_profile.c`, apontando para os `extern const vehicle_profile_t` de `profiles/`.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(vehicle_profile): perfis Sandero K7M e genérico manual`.

---

### Tarefa 5: `gear_engine`, aprendizado das relações

**Arquivos:**
- Criar: `components/gear_engine/{CMakeLists.txt, gear_learn.h, gear_learn.c}`
- Teste: `test/host/main/test_gear_learn.c`

**Interfaces:**
- Consome: `vehicle_profile_t`.
- Produz (header interno do componente):
```c
typedef struct { /* livre ao implementador; tamanho fixo, sem malloc */ } gear_learn_t;
void    gear_learn_init(gear_learn_t *l, uint8_t gears);
void    gear_learn_add(gear_learn_t *l, float ratio);         /* só amostras já filtradas como estáveis */
uint8_t gear_learn_result(const gear_learn_t *l, float out[VP_MAX_GEARS]); /* nº de marchas confiáveis; out em ordem crescente */
bool    gear_learn_converged(const gear_learn_t *l);           /* result == gears */
```
- Convenção: `out[0]` = 1ª marcha = menor km/h por 1000 RPM. A relação cresce com a marcha.
- O algoritmo de clustering é livre (spec). Restrições: memória fixa; um cluster só conta com ≥ 40 amostras e ≥ 5% do total; clusters a menos de 8% um do outro são fundidos.
- Filtro de amostra estável. Fica no `gear_engine` (Tarefa 7), não aqui: velocidade ≥ 10 km/h, RPM ≥ 1000, carga ≥ 10% ou acelerador > 3%, e a relação variando < 3% em relação à amostra anterior.

- [ ] **Passo 1: testes que falham.** Use um gerador determinístico (LCG com semente fixa) e ruído uniforme de ±2%.
  - `test_learns_five`: 300 amostras para cada relação `{7.5, 13.6, 20.0, 27.1, 36.9}` → `result == 5`, cada valor a menos de 3% do real, `converged` true.
  - `test_learns_six_gear_profile`: 6 relações `{7.0,12.0,17.5,23.0,29.0,35.0}` com `gears=6` → `result == 6`.
  - `test_transition_noise_rejected`: o mesmo do `test_learns_five` mais 150 amostras uniformes espalhadas entre 5 e 40 → continua `result == 5`, valores a menos de 3%.
  - `test_partial_never_fifth`: só as 4 primeiras relações → `result == 4`, `converged` false.
  - `test_too_few_samples`: 20 amostras de uma relação → `result == 0`.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar `gear_learn.c`.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(gear_engine): aprendizado automático das relações de marcha`.

---

### Tarefa 6: `gear_engine`, detecção da marcha com histerese

**Arquivos:**
- Criar: `components/gear_engine/{gear_detect.h, gear_detect.c}`
- Teste: `test/host/main/test_gear_detect.c`

**Interfaces:**
- Produz:
```c
typedef struct { uint8_t shown; uint8_t candidate; uint32_t candidate_since_ms; } gear_detect_t;
void    gear_detect_init(gear_detect_t *d);
/* Devolve a marcha exibida (1..n) ou 0 = desconhecida ("–"). ratios[0..n-1] crescentes; n = nº conhecidas. */
uint8_t gear_detect_update(gear_detect_t *d, const float *ratios, uint8_t n,
                           float rpm, float speed_kmh, float tolerance, uint16_t hold_ms, uint32_t now_ms);
```
- Regra: com `rpm < 500` ou `speed < 3`, o candidato é 0. Fora isso, o candidato é a marcha cuja relação fica dentro da tolerância de `speed/(rpm/1000)`, ou 0 se nenhuma ficar. `shown` só muda para o candidato depois que ele se mantém por `hold_ms`. **Exceção:** a mudança para 0 também espera `hold_ms`, para não piscar "–" na troca. Ela acontece quando o candidato 0 se mantém pelo `hold_ms`.

- [ ] **Passo 1: testes que falham** (relações `{7.5,13.6,20.0,27.1,36.9}`, tolerância 0.07, hold 400):
  - `test_detects_third`: 2000 RPM a 40 km/h, chamadas a cada 100 ms. Em t = 0..300 retorna 0, e em t ≥ 400 retorna 3.
  - `test_zero_rpm_or_speed`: `rpm=0`, `speed=50` e `rpm=900`, `speed=0` → 0, sem NaN (verificar com `isnan` em asserts internos via retorno).
  - `test_clutch_in_shows_dash`: em 3ª estável, depois 1500 RPM a 60 km/h (relação 40, sem casar) por 500 ms → 0.
  - `test_no_flicker_on_shift`: em 3ª estável, 200 ms sem casar e depois 4ª → nunca retorna 0 nesse intervalo, e chega a 4 depois de 400 ms de 4ª.
  - `test_partial_ratios`: `n=2`, a amostra casa com a 4ª real → 0.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(gear_engine): detecção de marcha com histerese`.

---

### Tarefa 7: `gear_engine`, conselheiro de troca e API pública

**Arquivos:**
- Criar: `components/gear_engine/{include/gear_engine.h, shift_advisor.h, shift_advisor.c, gear_engine.c}`
- Teste: `test/host/main/test_shift_advisor.c`, `test/host/main/test_gear_engine.c`

**Interfaces:**
- Consome: Tarefas 4, 5 e 6.
- Produz (`gear_engine.h`):
```c
typedef struct {
    uint32_t now_ms, sample_ms;         /* sample_ms = instante da amostra mais velha entre rpm/speed */
    float rpm, speed_kmh, load_pct;
    float throttle_pct;                  /* NAN se o carro não fornece */
} gear_input_t;
typedef enum { SHIFT_NONE, SHIFT_UP, SHIFT_DOWN } shift_advice_t;
typedef struct {
    uint8_t gear;          /* 0 = "–" */
    shift_advice_t advice;
    uint8_t target_gear;   /* 0 se advice == NONE */
    bool learning;         /* aprendizado ainda não convergiu */
    uint8_t learned_gears;
    bool stale;
} gear_state_t;
typedef struct { /* contém gear_learn_t, gear_detect_t, ratios, perfil, flags */ } gear_engine_t;
void         gear_engine_init(gear_engine_t *g, const vehicle_profile_t *p, const float *learned /* NULL ou p->gears valores */);
gear_state_t gear_engine_update(gear_engine_t *g, const gear_input_t *in);
bool         gear_engine_take_learned(gear_engine_t *g, float out[VP_MAX_GEARS]); /* true uma vez, logo após convergir */
void         gear_engine_reset_learning(gear_engine_t *g);
```
- Produz (`shift_advisor.h`):
`shift_advice_t shift_advise(const vehicle_profile_t *p, const float *ratios, uint8_t n, uint8_t gear, const gear_input_t *in, uint8_t *target);`
- Regras (spec):
  - **Limite de subida** = interpolação linear de `up_rpm_light` até `up_rpm_heavy` entre `load_light_pct` e `load_heavy_pct` (saturando fora da faixa).
  - **UP** se `gear < n`, `rpm > limite` e `rpm * ratios[gear-1] / ratios[gear] ≥ min_rpm_after_upshift`. O alvo é `gear + 1`.
  - **DOWN** se `rpm < lug_rpm`, `load ≥ lug_load_pct` e o acelerador não está fechado. O alvo é a **maior** marcha `k < gear` cujo RPM previsto `rpm * ratios[gear-1] / ratios[k-1]` cai em `[target_band_low, redline_rpm]` e fica o mais perto possível de `target_band_low..high`. Se nenhuma servir, o alvo é `gear - 1`.
  - **Acelerador fechado**: `throttle_pct ≤ 2`, ou, sem acelerador, `load_pct < 15`. Nesse caso nunca há DOWN (injeção cortada).
  - Sem conselho se `gear == 0`.
- `gear_engine_update`: `stale = now_ms - sample_ms > stale_ms`. Se stale → `gear = 0` e `advice = NONE` (o detector também zera). Usa as sementes do perfil enquanto o aprendizado não converge (só os valores ≠ 0). Quando converge, substitui as sementes pelas relações aprendidas, e `take_learned` passa a devolver true uma única vez.

- [ ] **Passo 1: testes que falham** (`test_shift_advisor.c`, perfil Sandero e relações `{7.5,13.6,20.0,27.1,36.9}`):
  - `test_upshift_light_load`: 3ª, 2200 RPM, carga 15 → UP, alvo 4.
  - `test_no_upshift_below_limit_heavy`: 3ª, 2500 RPM, carga 85 → NONE (limite 2800).
  - `test_no_upshift_if_lugs_next`: 1ª, 2200 RPM, carga 15 → previsto na 2ª = 2200·7.5/13.6 ≈ 1213 < 1500 → NONE.
  - `test_downshift_skips`: 5ª, 1200 RPM, carga 80, acelerador 70 → DOWN, e o alvo é a maior marcha com RPM previsto dentro da faixa (1200·36.9/20.0 ≈ 2214 → 3).
  - `test_no_downshift_throttle_closed`: 5ª, 1200 RPM, carga 80, acelerador 1 → NONE.
  - `test_no_downshift_closed_by_load_fallback`: o mesmo com `throttle = NAN` e carga 10 → NONE.
  - `test_top_gear_no_up`: 5ª, 3000 RPM → NONE.
  - **`test_gear_engine.c`**:
  - `test_stale_clears`: 3ª estável com UP; depois `now_ms - sample_ms = 1500` → `gear 0`, `NONE`, `stale` true.
  - `test_first_boot_no_seeds`: perfil genérico, `learned = NULL` → `learning` true, `learned_gears 0`, `gear 0`, sem crash.
  - `test_learns_then_reports_once`: alimenta 5 regimes estáveis (≥ 60 s simulados cada, a 10 Hz) → `take_learned` true uma vez, depois false; em seguida a marcha é detectada.
  - `test_six_gear_profile`: perfil `test_six_gear` com sementes → 6ª detectada e UP impossível na 6ª.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar `shift_advisor.c` e `gear_engine.c`, incluindo o filtro de amostra estável da Tarefa 5.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(gear_engine): conselheiro de troca econômico e API pública`.

---

### Tarefa 8: `obd_poller`

**Arquivos:**
- Criar: `components/obd_poller/{CMakeLists.txt, include/obd_poller.h, obd_poller.c}`
- Teste: `test/host/main/test_obd_poller.c`

**Interfaces:**
- Consome: as constantes `ELM_PID_*` (Tarefa 3). O poller não chama o transporte: quem faz a leitura é a task OBD (Tarefa 16).
- Produz:
```c
typedef struct { float value; uint32_t t_ms; bool valid; } obd_value_t;
typedef struct {
    obd_value_t rpm, speed_kmh, load_pct, throttle_pct, map_kpa, coolant_c, iat_c;
    float rate_hz;               /* respostas OK por segundo (média móvel) */
    uint8_t load_source_pid;     /* 0x04, ou 0x11 se não houver 0x04 */
} obd_snapshot_t;
typedef bool (*obd_supported_fn)(void *arg, uint8_t pid);
typedef struct { /* lista de itens, round-robin, snapshot */ } obd_poller_t;
void    obd_poller_init(obd_poller_t *p, obd_supported_fn supported, void *arg);
uint8_t obd_poller_next(obd_poller_t *p, uint32_t now_ms);           /* próximo PID a pedir */
void    obd_poller_record(obd_poller_t *p, uint8_t pid, float value, bool ok, uint32_t now_ms);
const obd_snapshot_t *obd_poller_snapshot(const obd_poller_t *p);
```
- Lista. **Rápidos**: 0C, 0D, carga (04, ou 11 como fonte de carga se faltar 04) e 11 (se suportado). **Lentos**: 05 a cada 5000 ms, 0B e 0F a cada 1000 ms (gravados para a Etapa 2). PID não suportado nunca entra.
- `next`: se algum lento estiver vencido, devolve o mais atrasado. Senão, o próximo rápido em round-robin.
- `rate_hz`: média móvel exponencial do intervalo entre respostas OK.

- [ ] **Passo 1: testes que falham:**
  - `test_fast_round_robin`: tudo suportado, chamadas a cada 50 ms → os 20 primeiros PIDs só contêm rápidos e lentos vencidos. A contagem de 0C e de 0D fica no mínimo 4× a de 05 em 10 s simulados.
  - `test_slow_when_due`: em t = 5000, `next` devolve 05.
  - `test_fallback_no_throttle`: sem 11 → 11 nunca é pedido, e `load_source_pid == 0x04`.
  - `test_fallback_no_load`: sem 04 e com 11 → `load_pct` é preenchido a partir de 11.
  - `test_record_and_rate`: 10 respostas OK a cada 100 ms → `rate_hz` entre 8 e 12. Uma falha (`ok=false`) não altera o valor anterior nem o timestamp.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(obd_poller): agenda de PIDs por prioridade com fallbacks`.

---

### Tarefa 9: `storage` (NVS)

**Arquivos:**
- Criar: `components/storage/{CMakeLists.txt, include/storage.h, storage.c}`
- Teste: `test/host/main/test_storage.c` (NVS no alvo linux, que a IDF suporta via esp_partition emulado; se precisar, adicione uma `partitions.csv` com nvs em `test/host`)

**Interfaces:**
- Produz (namespace NVS `"carmon"`):
```c
esp_err_t storage_init(void);   /* nvs_flash_init; apaga e reinicia em NO_FREE_PAGES/NEW_VERSION */
esp_err_t storage_load_ratios(const char *profile_id, float out[VP_MAX_GEARS], uint8_t *n); /* ESP_ERR_NOT_FOUND */
esp_err_t storage_save_ratios(const char *profile_id, const float *ratios, uint8_t n);       /* blob "r_<id>" */
esp_err_t storage_erase_ratios(const char *profile_id);
bool      storage_get_mute(void);              esp_err_t storage_set_mute(bool m);
esp_err_t storage_get_elm_mac(uint8_t mac[6]); esp_err_t storage_set_elm_mac(const uint8_t mac[6]);
esp_err_t storage_get_profile_id(char *out, size_t cap); esp_err_t storage_set_profile_id(const char *id);
```
- O blob de relações é `{uint8_t version=1; uint8_t n; float r[n]; uint32_t crc32}`. Na leitura, CRC ou versão inválidos → `ESP_ERR_INVALID_CRC`, tratado como "sem dados".

- [ ] **Passo 1: testes que falham:** `test_ratios_roundtrip` (5 valores salvos e lidos iguais); `test_missing_is_not_found`; `test_corrupt_blob_rejected` (grava um blob de lixo direto via `nvs_set_blob` → `ESP_ERR_INVALID_CRC`); `test_mute_default_false_then_true`; `test_mac_roundtrip`.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar. Use `esp_crc32_le` do `esp_rom`; se não estiver disponível no linux, use uma CRC32 local.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(storage): persistência NVS com validação de integridade`.

---

### Tarefa 10: `ui`, parte lógica (layout, cores, alertas)

**Arquivos:**
- Criar: `components/ui/{CMakeLists.txt, include/ui.h, include/ui_logic.h, ui_layout.c, ui_colors.c, ui_alert.c}`
- Teste: `test/host/main/test_ui_logic.c`

**Interfaces:**
- Consome: `gear_state_t`, `shift_advice_t` (Tarefa 7).
- Produz (`ui_logic.h`):
```c
typedef struct { int16_t x, y, w, h; } ui_rect_t;
typedef struct { ui_rect_t gear, target, status; uint16_t gear_font_px; } ui_layout_t;
ui_layout_t ui_layout_compute(uint16_t width, uint16_t height);
typedef enum { UI_BG_OK, UI_BG_UP, UI_BG_DOWN, UI_BG_NODATA } ui_bg_t;
ui_bg_t  ui_bg_for(const gear_state_t *s, bool connected);
uint32_t ui_bg_rgb(ui_bg_t bg);                 /* 0x1B8A3A, 0x1565C0, 0xEF6C00, 0x424242 */
typedef enum { UI_TONE_NONE, UI_TONE_UP, UI_TONE_DOWN } ui_tone_t;
typedef struct { shift_advice_t last; uint32_t last_tone_ms; bool armed; bool any_tone; } ui_alert_t;
void      ui_alert_init(ui_alert_t *a);
ui_tone_t ui_alert_update(ui_alert_t *a, shift_advice_t advice, uint32_t now_ms, bool muted);
```
- Layout: a marcha fica centralizada e ocupa ≥ 60% da altura; o alvo ("→ 4") fica à direita, no modo paisagem, ou abaixo, no modo retrato; o status ocupa uma faixa fina embaixo. Tudo proporcional, sem constantes de 240×135.
- Alerta: toca na transição para UP ou DOWN, inclusive na troca direta UP↔DOWN. Volta a armar quando o conselho volta a NONE. Intervalo mínimo de 3000 ms. Com mute, nunca toca.

- [ ] **Passo 1: testes que falham:**
  - `test_layout_fits_all`: para `(240,135)`, `(320,240)` e `(135,240)`, todos os retângulos ficam dentro da tela, `gear.h ≥ 0.6*h`, e `gear` não se sobrepõe a `target`.
  - `test_bg_mapping`: UP → `UI_BG_UP`; desconectado → `NODATA`; stale → `NODATA`; NONE → OK.
  - `test_alert_once`: UP mantido por 10 s com chamada a cada 100 ms → um único `UI_TONE_UP`.
  - `test_alert_min_interval`: UP, NONE, UP dentro de 1 s → o segundo UP não toca; um UP depois de 3 s toca.
  - `test_alert_muted`: com mute → sempre NONE.
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(ui): layout adaptável, cores e política de bipe`.

---

### Tarefa 11: `power_mgr`, decisão de dormir

**Arquivos:**
- Criar: `components/power_mgr/{CMakeLists.txt, include/power_mgr.h, power_policy.c}` (o `power_mgr.c`, ligado ao hardware, entra na Tarefa 16)
- Teste: `test/host/main/test_power.c`

**Interfaces:**
- Produz:
```c
typedef struct {
    uint32_t now_ms, last_engine_running_ms, last_elm_ok_ms;
    float battery_v;               /* NAN = desconhecida */
} power_inputs_t;
#define POWER_IDLE_MS     (5u * 60u * 1000u)
#define POWER_SLEEP_V     13.0f
#define POWER_WAKE_MS     (10u * 60u * 1000u)
#define POWER_RUN_V       13.2f
bool power_should_sleep(const power_inputs_t *in);
bool power_should_stay_awake_after_wake(float battery_v, float rpm);   /* v ≥ 13.2 ou rpm > 0 */
```
- Regra (spec): dorme se `now - max(last_engine_running, last_elm_ok) ≥ 5 min` **e** (`battery_v < 13.0` ou `battery_v` é NAN). Detalhe: ELM sem resposta conta como "parado", e nesse caso a tensão é NAN.

- [ ] **Passo 1: testes que falham:** `test_running_no_sleep` (motor rodando há 1 s → false); `test_idle_low_v_sleeps` (6 min parado e 12.4 V → true); `test_idle_high_v_no_sleep` (6 min e 13.8 V, ex. carregador → false); `test_elm_gone_sleeps` (6 min sem ELM, NAN → true); `test_wake_rules` (13.5 V → true; 12.5 V e rpm 0 → false; 12.5 V e rpm 800 → true).
- [ ] **Passo 2:** **HOST** → falha.
- [ ] **Passo 3:** Implementar.
- [ ] **Passo 4:** **HOST** → `0 Failures`.
- [ ] **Passo 5:** Commit `feat(power_mgr): política de sono por inatividade e tensão`.

---

### Tarefa 12: `board_api`, BSP M5StickC Plus2, BSP headless e tela LVGL

**Arquivos:**
- Criar: `components/board_api/{CMakeLists.txt, Kconfig, include/board_api.h}`
- Criar: `components/bsp_m5stickc_plus2/{CMakeLists.txt, idf_component.yml, bsp.c}`, `components/bsp_headless/{CMakeLists.txt, bsp.c}`
- Criar: `components/ui/ui_screen.c` (LVGL)
- Modificar: `main/main.c` (demo temporária de hardware)

**Interfaces:**
- Produz (`board_api.h`):
```c
typedef enum { BOARD_BTN_A, BOARD_BTN_B, BOARD_BTN_PWR } board_button_t;
typedef enum { BOARD_PRESS_SHORT, BOARD_PRESS_LONG } board_press_t;   /* long ≥ 2000 ms */
typedef void (*board_button_cb_t)(board_button_t b, board_press_t p, void *arg);
esp_err_t     board_init(void);
lv_display_t *board_display(void);                 /* NULL no headless */
bool          board_lvgl_lock(uint32_t timeout_ms);
void          board_lvgl_unlock(void);
esp_err_t     board_buzzer_tone(uint16_t freq_hz, uint16_t ms);   /* ESP_ERR_NOT_SUPPORTED no headless */
esp_err_t     board_buttons_register(board_button_cb_t cb, void *arg);
void          board_deep_sleep(uint32_t wake_after_ms);           /* acorda por timer ou BTN_A */
```
- Produz (`ui.h`, depende de LVGL): `esp_err_t ui_init(lv_display_t *disp);` (disp NULL = só log), `void ui_show_gear(const gear_state_t *s, bool connected);`, `void ui_show_diag(const ui_diag_t *d);` e `void ui_toggle_screen(void);`. `ui_diag_t` traz `{const char *status; const char *protocol; uint8_t n_supported; float rate_hz, rpm, speed_kmh, battery_v;}`. `ui_screen.c` usa `ui_layout_compute` com a resolução de `lv_display_get_horizontal_resolution/vertical`, e `ui_bg_rgb`.
- Kconfig `board_api`: `choice BOARD` com `BOARD_M5STICKC_PLUS2` (`depends on IDF_TARGET_ESP32`) e `BOARD_HEADLESS`. Cada BSP faz `idf_component_register()` vazio e `return()` quando não está selecionado.
- Plus2 (confirmar no esquemático da M5Stack):
  - LCD ST7789V2 135×240: MOSI 15, SCLK 13, DC 14, RST 12, CS 5, backlight 27. Em paisagem: `swap_xy`, gap x 40 / y 53, cores invertidas.
  - Buzzer 2 (LEDC). Botões A=37, B=39, PWR=35. HOLD=4: nível alto no boot, `gpio_hold_en` antes de dormir.
  - Dependências `espressif/esp_lvgl_port` e `lvgl/lvgl` 9.x em `idf_component.yml`.
- Botões, mapeamento decidido por mim: A curto = mute; A longo = reaprender marchas; B curto = alterna tela marcha/diagnóstico; B longo = re-parear o ELM.

- [ ] **Passo 1:** Implementar a API, os dois BSPs e `ui_screen.c`. `main.c` temporário: cicla os 4 fundos (OK, UP, DOWN, NODATA) com uma marcha falsa a cada 2 s, toca o tom de subir e de reduzir, e loga os botões.
- [ ] **Passo 2:** `idf.py set-target esp32 build flash monitor` (com `BOARD_M5STICKC_PLUS2`). Esperado no aparelho: tela deitada sem faixa de lixo nas bordas, 4 cores, marcha grande centralizada, bipes distintos, e cada botão logado como curto ou longo.
- [ ] **Passo 3:** `idf.py set-target esp32c3 build` (com `BOARD_HEADLESS`, que deve ser o default nesse alvo). Esperado: build OK.
- [ ] **Passo 4:** **HOST** continua `0 Failures`. A `ui` lógica não pode ter ganhado dependência de LVGL: `ui_logic.h` fica sem LVGL, e o componente separa as fontes puras.
- [ ] **Passo 5:** Commit `feat(board): API de placa, BSPs M5StickC Plus2 e headless, tela LVGL`.

---

### Tarefa 13: `transport_bt_spp`

**Arquivos:**
- Criar: `components/obd_transport/{transport_bt_spp.c, include/transport_bt_spp.h}`
- Modificar: `components/obd_transport/CMakeLists.txt` (fonte condicional a `CONFIG_OBD_TRANSPORT_BT_SPP`, e `REQUIRES bt` só nesse caso)

**Interfaces:**
- Produz:
```c
typedef struct {
    const char *name;        /* CONFIG_OBD_ELM_BT_NAME */
    const char *pin;         /* CONFIG_OBD_ELM_BT_PIN */
    const uint8_t *mac;      /* NULL = procurar por nome */
} transport_bt_spp_config_t;
esp_err_t transport_bt_spp_create(const transport_bt_spp_config_t *cfg, obd_transport_t **out);
esp_err_t transport_bt_spp_get_peer(obd_transport_t *t, uint8_t mac[6]);
```
- Base: exemplo `examples/bluetooth/bluedroid/classic_bt/bt_spp_initiator` da IDF v6.1. PIN legado fixo com `esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, ...)`. Busca o nome no EIR e no `ESP_BT_GAP_DISC_RES_EVT`, faz a descoberta SPP e conecta. `ESP_SPP_DATA_IND_EVT` alimenta um `StreamBuffer` de 1 KB. `read` consome do StreamBuffer com timeout. `ESP_SPP_CLOSE_EVT` faz `is_connected = false`, e as operações seguintes devolvem `ESP_ERR_INVALID_STATE`.

- [ ] **Passo 1:** Implementar. `main.c` temporário: cria o transporte, faz `elm327_init` e loga o protocolo, os PIDs suportados e 20 leituras de `010C`.
- [ ] **Passo 2:** No carro, com o celular sem Bluetooth: `idf.py flash monitor`. Esperado: `connected`, o protocolo (ex. `A5` ou `A6`), a lista de PIDs e o RPM variando quando você acelera parado. Anote o protocolo e os PIDs numa seção nova "Achados no carro" no fim do spec.
- [ ] **Passo 3:** Desligue o ELM com o aparelho conectado. Esperado: log de desconexão e `obd_transport_command` devolvendo `ESP_ERR_INVALID_STATE`, sem travar.
- [ ] **Passo 4:** `idf.py set-target esp32c3 build`. Esperado: OK, sem o SPP.
- [ ] **Passo 5:** Commit `feat(obd_transport): transporte Bluetooth clássico SPP`.

---

### Tarefa 14: `obd_recorder`, sinks e console

**Arquivos:**
- Criar/Modificar: `components/obd_recorder/{obd_recorder.c, include/obd_recorder.h, CMakeLists.txt, idf_component.yml}` (adiciona `joltwallet/littlefs`)

**Interfaces:**
- Consome: `rec_format_line` (Tarefa 2) e `elm327_tap_t` (Tarefa 3).
- Produz:
```c
typedef enum { REC_SINK_SERIAL = 1, REC_SINK_FLASH = 2 } rec_sink_t;
esp_err_t rec_init(void);                       /* monta LittleFS da partição "storage" */
esp_err_t rec_start(int sinks);                 /* cria /littlefs/trip_NNN.tsv */
void      rec_tap(void *arg, const char *cmd, const char *resp);   /* compatível com elm327_tap_t; usa esp_log_timestamp() */
void      rec_stop(void);
esp_err_t rec_console_register(void);           /* comandos: rec on [serial|flash|both], rec off, rec ls, rec dump <n>, rec clear */
```
- O sink serial imprime `REC\t<linha>` via `printf`. Para capturar: `idf.py monitor | grep '^REC' | cut -f2- > viagem.tsv`, documentado no README. O sink de flash faz `fflush` a cada 2 s, e se a flash enche, para de gravar com log de aviso. `rec dump` imprime o arquivo no mesmo formato `REC\t...`.

- [ ] **Passo 1:** Implementar e registrar o console (REPL na UART0) no `main.c` temporário.
- [ ] **Passo 2:** No aparelho: `rec on both`, rodar 1 min, `rec off`, `rec ls` (o arquivo aparece com tamanho > 0), `rec dump 0 | grep REC` (as linhas passam em `rec_parse_line`; para conferir, use o replay tool da Tarefa 15).
- [ ] **Passo 3:** Reiniciar o aparelho e `rec ls`. O arquivo continua lá.
- [ ] **Passo 4:** Commit `feat(obd_recorder): gravação serial e LittleFS com comandos de console`.

---

### Tarefa 15: ferramenta de replay no host

**Arquivos:**
- Criar: `tools/replay/{CMakeLists.txt, sdkconfig.defaults, main/CMakeLists.txt, main/replay_main.c}`, `tools/replay/sample.tsv` (gravação sintética de ~30 s: 2ª → 3ª → 4ª)

**Interfaces:**
- Consome: `transport_replay_create`, `transport_replay_now_ms`, `elm327_*`, `obd_poller_*`, `gear_engine_*`, `vp_find`.
- Uso: `./tools/replay/build/replay.elf <arquivo.tsv> [profile_id]` → stdout CSV `t_ms,rpm,speed,load,gear,advice,target,learning`. O relógio é o `transport_replay_now_ms`, sem esperas reais. O loop termina quando o replay esgota (`ESP_ERR_NOT_FOUND` contínuo ou EOF exposto pelo replay).

- [ ] **Passo 1:** Escrever `sample.tsv` com respostas coerentes às relações `{7.5,13.6,20.0,27.1,36.9}` e as sementes passadas por um perfil de teste (flag `--seeds` com 5 floats). Implementar `replay_main.c`.
- [ ] **Passo 2:** `idf.py -C tools/replay set-target linux build && ./tools/replay/build/replay.elf tools/replay/sample.tsv sandero_k7m --seeds 7.5,13.6,20.0,27.1,36.9`. Esperado: a coluna gear passa por 2, depois 3, depois 4, e aparece `advice=UP` antes de cada troca.
- [ ] **Passo 3:** Commit `feat(tools): replay de gravações no host`.

---

### Tarefa 16: integração do app e builds

**Arquivos:**
- Criar: `main/app_obd.c/.h`, `main/app_ui.c/.h`, `main/Kconfig.projbuild`, `components/power_mgr/power_mgr.c`
- Modificar: `main/main.c`, `README.md`

**Interfaces:**
- Consome: tudo acima.
- `app_obd` (task, prioridade 5, stack 6 KB):
  - Conecta com o MAC do NVS, ou por nome. Se conectar por nome, salva o MAC.
  - Backoff de reconexão de 2 s a 30 s.
  - Faz `elm327_init` e `obd_poller_init(elm327_pid_supported)`, e entra no loop `next` → `elm327_read_pid` → `record`. ATRV a cada 10 s.
  - Publica `obd_snapshot_t` + `connected` + `battery_v` + diagnóstico sob mutex (`app_obd_get(app_obd_status_t *out)`).
  - Erro de transporte → desconecta e reconecta.
  - Tap → `rec_tap` quando a gravação está ligada.
- `app_ui` (task, 20 Hz):
  - Lê o status e monta o `gear_input_t`. `sample_ms` = o menor entre `rpm.t_ms` e `speed.t_ms`, e `throttle` vira NAN se não for suportado.
  - Chama `gear_engine_update`. Se `take_learned` → `storage_save_ratios`.
  - Atualiza `ui_show_gear`/`ui_show_diag`, chama `ui_alert_update` e toca `board_buzzer_tone`.
  - Trata os botões (mapeamento da Tarefa 12).
  - Chama `power_should_sleep` a cada 1 s, e se der true → `rec_stop` e `board_deep_sleep(POWER_WAKE_MS)`.
- Ao acordar por timer, `power_should_stay_awake_after_wake` decide, com até 30 s de tentativa de conexão. Se a decisão for não ficar acordado, volta a dormir.
- Tela inicial: diagnóstico (Etapa 0) até haver dados. Depois, tela de marcha, alternável pelo botão B.
- `Kconfig.projbuild`: `APP_PROFILE_ID` (string, default "sandero_k7m"), sobrescrito pelo NVS se definido.

- [ ] **Passo 1:** Implementar e remover os `main.c` temporários.
- [ ] **Passo 2:** **HOST** → `0 Failures`. `idf.py set-target esp32 build` OK. `idf.py set-target esp32c3 build` OK, com headless e sem SPP.
- [ ] **Passo 3:** No carro (checklist da Etapa 0):
  - A tela de diagnóstico mostra o protocolo, o nº de PIDs, a taxa (Hz), RPM, velocidade e tensão.
  - Desligar e religar o ELM → reconecta sozinho.
  - Ligar `rec on both`, rodar uma viagem de 10 min e exportar com `rec dump`.
  - Rodar o replay tool com a gravação real e anotar se a marcha foi aprendida.
- [ ] **Passo 4:** Com o carro desligado e o USB ligado, o aparelho dorme depois de cerca de 5 min. Para testar rápido, use um Kconfig de debug `APP_POWER_IDLE_MS_OVERRIDE`. Acorda pelo botão A.
- [ ] **Passo 5:** No README: como ativar a IDF 6.1, compilar, gravar, capturar uma viagem, rodar o replay e mapear os botões. Avisar que o celular não pode estar conectado ao ELM ao mesmo tempo.
- [ ] **Passo 6:** Commit `feat(app): integração OBD, gear_engine, UI, gravação e energia`.

---

## Depois deste plano (segundo ciclo curto da Etapa 1, fora deste plano)

Ajustar os limiares do perfil Sandero com gravações reais pelo replay tool e validar no carro o checklist do spec: as 5 marchas, "–" com a embreagem pisada, um bipe por limite e as cores.

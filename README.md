# Car-Monitor-ESP32

Assistente de troca de marchas para carro manual, num **M5StickC Plus2** (ESP32) ligado por
Bluetooth clássico a um adaptador **ELM327**. O aparelho aprende sozinho as relações das
marchas, mostra a marcha atual em tamanho grande e avisa, pela cor da tela e por um bipe,
quando subir ou reduzir (e para qual marcha), pensando em economia.

- Fundo **verde** = ok, **azul** = suba, **laranja** = reduza, **cinza** = sem dados ou desconectado.
- O aprendizado é salvo no NVS assim que converge; tudo o que é específico do carro mora num
  perfil (`components/vehicle_profile/profiles/`).
- Com o motor parado e a bateria sem carga, o aparelho entra em sono profundo sozinho.

Projeto e decisões: `docs/superpowers/specs/2026-10-25-assistente-marchas-design.md`.

> **Atenção:** o ELM327 SPP aceita uma conexão só. Desconecte o celular (ou desligue o
> Bluetooth dele) antes de usar o aparelho, senão o ESP32 não consegue conectar.

## Pré-requisitos

- **ESP-IDF v6.1** (nada de Arduino). Em cada terminal novo:

      . ~/.espressif/tools/activate_idf_v6.1.sh

- M5StickC Plus2 (alvo `esp32`, 8 MB de flash) e um ELM327 Bluetooth clássico.

## Compilar, gravar e monitorar

    idf.py set-target esp32     # só na primeira vez (ou ao trocar de alvo)
    idf.py build
    idf.py -p /dev/ttyUSB0 flash monitor

O projeto também compila para `esp32c3` (`idf.py set-target esp32c3 build`): sem Bluetooth
clássico, sai sem transporte SPP e com o BSP headless (tudo no log). O app sobe, loga
"sem transporte disponível" e fica parado.

## Configuração (`idf.py menuconfig`)

| Menu | Item | Padrão |
|---|---|---|
| Placa | Placa (BSP): M5StickC Plus2 ou headless | Plus2 no `esp32` |
| M5StickC Plus2 | Pinos (LCD, buzzer, botões, HOLD) e ajustes do LCD (gap, inversão, espelhamento) | valores do esquemático, a conferir |
| OBD transporte | Nome Bluetooth do ELM327 (`OBD_ELM_BT_NAME`) | `OBDII` |
| OBD transporte | PIN de pareamento (`OBD_ELM_BT_PIN`) | `1234` |
| OBD poller | Gravação estendida: pedir também MAP e IAT (`OBD_POLL_EXTENDED`) | desligado |
| Car Monitor (app) | Perfil de veículo padrão (`APP_PROFILE_ID`): `sandero_k7m` ou `generic_man` | `sandero_k7m` |
| Car Monitor (app) | Ociosidade até dormir em ms (`APP_POWER_IDLE_MS_OVERRIDE`, depuração; 0 = 5 min) | `0` |

Um perfil salvo no NVS tem prioridade sobre `APP_PROFILE_ID`; um id desconhecido cai no
perfil padrão (`sandero_k7m`).

## Uso

Na primeira vez o aparelho procura o adaptador pelo nome, conecta com o PIN e salva o MAC no
NVS; nas próximas vai direto ao MAC. Se o link cair, reconecta sozinho (espera de 2 s, dobrando
até 30 s entre tentativas). Respostas estranhas do ELM (`CAN ERROR`, `BUS BUSY`, timeouts com
o link de pé) não derrubam o link: 3 seguidas reinicializam o ELM no mesmo link.

O conselho de troca só aparece depois de ~0,4 s e só some com folga (UP abaixo do limite
− 150 RPM, DOWN acima de 1300 + 150 RPM ou ao tirar o pé); com a embreagem pisada ou no meio da
troca não há conselho. O bipe toca uma vez por evento e só volta depois de 2 s sem conselho ou
de uma troca de marcha. Com um ELM lento (K-line, ~4 leituras/s) o tempo de "sem dados" e a
histerese da marcha crescem com a taxa medida.

A tela começa no **diagnóstico** (status da conexão, protocolo, nº de PIDs suportados, taxa de
leitura, RPM, velocidade e tensão da bateria). Quando chegam RPM e velocidade, troca sozinha
para a tela de **marcha**.

### Botões

| Botão | Curto | Longo (≥ 2 s) |
|---|---|---|
| A (frente) | liga/desliga o bipe (mudo, salvo no NVS) | reaprender as marchas (apaga as relações salvas) |
| B (lateral) | alterna tela de marcha / diagnóstico | re-parear o ELM (esquece o MAC e procura pelo nome) |

Use o re-pareamento ao trocar de adaptador: com um MAC salvo o aparelho não procura outro.

### Energia

Sem nenhuma leitura de RPM > 0 por 5 min (motor parado com a ECU respondendo RPM 0, ou o ELM
mudo) **e** a bateria abaixo de 13,0 V (ou desconhecida), o aparelho para a gravação e dorme.
Com a ignição ligada e o motor parado ele também dorme. Acorda pelo botão A, ou a
cada 10 min pelo timer: aí tenta conectar por até 30 s e só continua acordado se a tensão for
≥ 13,2 V ou o RPM for > 0 (volta a dormir assim que lê uma tensão menor e o ELM já respondeu ao pedido de RPM sem RPM > 0). Para testar rápido, ajuste `APP_POWER_IDLE_MS_OVERRIDE`
(ex.: `30000`).

## Gravação de viagens (obd_recorder)

Console na UART (`idf.py monitor`, prompt `carmon> `):

- `rec on [serial|flash|both]` inicia a gravação (padrão: both); `rec off` para.
- `rec ls` lista `/littlefs/trip_NNN.tsv`, uso da flash e contadores; `rec dump <n>` imprime a viagem n; `rec clear` apaga todas.

Cada linha sai como `REC<TAB>t_ms<TAB>CMD<TAB>RESP`. A gravação começa com a inicialização do
ELM (mesmo se ligada no meio da sessão), para o replay conseguir reproduzi-la. Para obter o
arquivo reproduzível no PC:

    idf.py monitor | grep '^REC' | cut -f2- > viagem.tsv

Sem notebook no carro: `rec on flash` (ou `both`), rode a viagem e, depois, com o USB no PC,
`rec ls` e `rec dump <n>` com o mesmo `grep`/`cut` acima. A gravação para sozinha antes do
sono profundo.

## Replay de uma gravação no PC

`tools/replay` roda o `elm327`, o `obd_poller` e o `gear_engine` reais sobre um `.tsv`, sem
hardware (alvo linux; veja o PATH em "Testes no host"):

    idf.py --preview -C tools/replay build
    ./tools/replay/build/replay.elf viagem.tsv [profile_id] [--seeds a,b,c,d,e]

Saída em CSV: `t_ms,rpm,speed,load,gear,advice,target,learning`. `tools/replay/sample.tsv` é
uma gravação sintética de exemplo.

## Testes no host

Os builds do alvo linux usam o gcc do PC. Depois de ativar a IDF, tire do PATH os binários
das toolchains cruzadas (o `as` delas quebra o gcc do host):

    . ~/.espressif/tools/activate_idf_v6.1.sh
    export PATH=$(echo "$PATH" | tr ':' '\n' | grep -vE 'xtensa|riscv|ulp' | paste -sd:)
    idf.py --preview -C test/host set-target linux build    # set-target só na primeira vez
    ./test/host/build/host_test.elf                          # sucesso = "0 Failures"

Use um terminal separado para isso: o PATH alterado não serve para compilar o firmware.

## Checklist no carro

Ainda não validado no carro. Na primeira saída, conferir:

1. **Conexão:** celular desconectado do ELM; ligar a ignição; o aparelho mostra
   "Conectando..." e depois "Conectado". O log mostra `MAC do ELM salvo: ...` na primeira vez.
2. **Diagnóstico:** a tela mostra protocolo (ex.: `A6`), nº de PIDs, taxa (Hz), RPM, velocidade
   e tensão (≈ 12,x V motor parado, ≈ 14 V ligado). Anotar protocolo e taxa.
3. **Troca de tela:** com o motor ligado, troca sozinha para a tela de marcha; B curto alterna.
4. **Reconexão:** tirar o ELM da tomada OBD e recolocar; o fundo fica cinza ("desconectado") e
   reconecta sozinho em até ~30 s.
5. **Viagem gravada:** `rec on both`, rodar ~10 min passando por todas as marchas, `rec off`,
   exportar com `rec dump <n>` e rodar o replay; anotar se a marcha foi aprendida
   (`learning` vira 0) e quantas marchas apareceram.
6. **Marcha e conselho:** marcha certa nas 5 marchas, "-" com a embreagem pisada, azul/laranja
   nos momentos esperados e um bipe só por evento; A curto silencia.
   - Desacelerando com o pé fora abaixo de 1300 RPM: **nunca** laranja.
   - Mantendo o giro em cima do limite de subida (oscilando um pouco): **um bipe só**, e o
     fundo fica azul sem piscar.
7. **Reaprender e re-parear:** A longo volta a "aprendendo"; B longo procura o ELM pelo nome.
8. **Sono:** carro desligado, USB ligado: dorme em ~5 min (ou no tempo do
   `APP_POWER_IDLE_MS_OVERRIDE`) e acorda pelo botão A. Com a ignição ligada e o motor
   parado também dorme em ~5 min. Depois de 10 min dormindo, acorda,
   tenta o ELM por até 30 s e volta a dormir.
9. **Stack:** anotar a linha `stack livre mínima: app_obd N B, app_ui N B` do log (sai aos
   60 s de uso e a cada `rec off`, de preferência depois da viagem gravada). Abaixo de ~1 KB,
   aumentar a stack da task.

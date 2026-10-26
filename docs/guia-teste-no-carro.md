# Guia de teste no carro

Passo a passo para o primeiro teste do Car Monitor no carro: preparar a placa, conectar ao
ELM327, capturar o log, gravar uma viagem (com ou sem notebook) e exportar os dados. O
checklist completo do que conferir está em [README.md](../README.md#checklist-no-carro).

## 0. Antes de começar

Em cada terminal novo, ative a ESP-IDF e entre no projeto:

    . ~/.espressif/tools/activate_idf_v6.1.sh
    cd ~/Documents/repositorios/Car-Monitor-ESP32

Descubra a porta serial da placa (com o M5StickC Plus2 ligado no USB):

    ls /dev/tty{ACM,USB}*

Nos comandos abaixo, troque `/dev/ttyACM0` pela porta que aparecer (pode ser `/dev/ttyUSB0`).

**Bluetooth do celular desligado de vez.** O ELM327 aceita uma conexão só, e apps como o
Car Scanner reconectam sozinhos em segundo plano. "Esquecer" o aparelho não basta: desligue o
Bluetooth do celular durante o teste.

## 1. Configurar e gravar o firmware

    idf.py menuconfig

| Menu | Item | Quando mudar |
|---|---|---|
| OBD transporte | Nome Bluetooth do ELM327 | se o adaptador não se chamar `OBDII` |
| OBD transporte | PIN de pareamento | se não for `1234` |
| Car Monitor (app) | Gravar a viagem na flash automaticamente | **ligue** se não for levar notebook no carro |
| Car Monitor (app) | Ociosidade até o sono profundo | só para testar o sono rápido (ex.: `30000`) |

Salve (`S`), saia (`Q`) e grave:

    idf.py build
    idf.py -p /dev/ttyACM0 flash

## 2. Teste na bancada (sem carro, ~15 min)

    idf.py -p /dev/ttyACM0 monitor        # sair: Ctrl+]

- **Tela:** fundo cinza com `Conectando...`. Imagem deslocada, espelhada ou com cores
  invertidas → `menuconfig` → *M5StickC Plus2* (gap, inversão, espelhamento). Tela apagada →
  conferir os pinos do LCD no esquemático da M5Stack.
- **Alimentação:** tire o USB; a placa deve continuar ligada pela bateria. Se desligar na
  hora, o pino HOLD (GPIO4) está errado.
- **Botões:** B curto alterna diagnóstico/marcha; A curto liga e desliga o mudo (aparece no log).
- **Console:** aperte Enter, deve aparecer `carmon>`; `rec ls` lista o uso da flash.
- **Log:** procure linhas com `E (` no boot.

## 3. Primeira conexão no carro (com notebook)

Ligue a ignição (motor desligado), conecte a placa no notebook e grave o log num arquivo:

    script -c "idf.py -p /dev/ttyACM0 monitor" log-bt.txt

Deixe 1–2 minutos (umas 3 tentativas de conexão) e saia com `Ctrl+]`. O arquivo
`log-bt.txt` fica na pasta do projeto.

### Como ler o log

Procure as linhas com `bt_spp` e `app_obd`:

| O que aparece | Onde parou |
|---|---|
| `procurando "OBDII"`, nenhum `visto ... nome "OBDII"`, depois `não encontrado na busca` | a placa não enxerga o ELM (celular conectado? nome errado?) |
| `adaptador "OBDII" encontrado`, depois `serviço SPP não encontrado` | o ELM respondeu à busca mas não ofereceu a porta serial |
| `falha no pareamento, status N` | PIN recusado |
| `conexão recusada` ou `SPP open falhou` | o ELM recusou a conexão (ocupado com outro aparelho?) |
| `connected 00:10:cc:...`, depois `elm327_init: ESP_ERR_TIMEOUT` ou `INVALID_RESPONSE` | Bluetooth ok; o ELM não respondeu como esperado |
| `conectado: protocolo ..., N PIDs` e `MAC do ELM salvo` | **tudo certo** |
| nenhuma linha `bt_spp` | o Bluetooth nem inicializou |

Com a conexão ok, a tela de diagnóstico mostra protocolo, nº de PIDs, taxa (Hz) e tensão.
**Anote esses quatro valores.** Ligue o motor: a tensão sobe para ≈ 14 V e a tela troca
sozinha para a de marcha.

## 4. Gravar uma viagem

O que gravar: 10–15 min passando pelas 5 marchas, com **alguns segundos de velocidade
constante em cada uma** (é assim que a placa aprende as relações). Inclua arrancadas,
reduções e desacelerações com o pé fora do acelerador.

Não mexa na placa dirigindo: ligue a gravação antes de sair ou peça para alguém operar.

### Com notebook

No monitor (`idf.py -p /dev/ttyACM0 monitor`), aperte Enter até ver `carmon>`. Os logs passam
por cima do que você digita, mas o comando chega à placa:

    carmon> rec on both      # grava na serial e na flash
    ... viagem ...
    carmon> rec off

O notebook alimenta a placa pelo USB. A gravação não sobrevive a um reinício: trocar o cabo
do notebook para o USB do carro reinicia a placa e encerra a gravação.

### Sem notebook

Ligue **Gravar a viagem na flash automaticamente** no `menuconfig` (passo 1) e grave o
firmware. Depois é só usar:

- a gravação começa sozinha quando o motor pega (primeira leitura de RPM > 0);
- para sozinha antes de a placa dormir (motor parado ~5 min);
- cada vez que a placa liga ou acorda e o motor pega, uma viagem nova (`trip_000.tsv`,
  `trip_001.tsv`, ...);
- cabem ~5 MB (umas 2 h); com a flash cheia a gravação para e as viagens antigas ficam.

A gravação só começa depois que a placa conecta ao ELM: confira primeiro a conexão (passo 3).

## 5. Exportar as viagens no PC

Com a placa no notebook:

    idf.py -p /dev/ttyACM0 monitor
    carmon> rec ls          # lista trip_NNN.tsv, uso da flash e contadores

Para salvar uma viagem num arquivo, abra o monitor filtrando as linhas da gravação:

    idf.py -p /dev/ttyACM0 monitor | grep '^REC' | cut -f2- > viagem.tsv

Digite `rec dump 1` (o número da viagem no `rec ls`) e Enter, espere terminar de imprimir e
saia com `Ctrl+]`. Repita com outro nome de arquivo para cada viagem. Depois de exportar
tudo, libere a flash com `rec clear`.

Com `rec on both`, a viagem também saiu na serial e dá para recortar o `log-bt.txt` do
mesmo jeito: `grep '^REC' log-bt.txt | cut -f2- > viagem.tsv`.

## 6. Conferir a viagem no PC (opcional)

O replay roda a lógica real sobre a gravação. Num terminal separado (o PATH alterado não
serve para gravar o firmware):

    . ~/.espressif/tools/activate_idf_v6.1.sh
    export PATH=$(echo "$PATH" | tr ':' '\n' | grep -vE 'xtensa|riscv|ulp' | paste -sd:)
    idf.py --preview -C tools/replay build
    ./tools/replay/build/replay.elf viagem.tsv

Saída em CSV: `t_ms,rpm,speed,load,gear,advice,target,learning`. `learning` virando 0 indica
que as marchas foram aprendidas.

## 7. O que mandar de volta

- protocolo, nº de PIDs, taxa (Hz) e tensão do diagnóstico;
- `log-bt.txt` (ou, se não conectou, ao menos o trecho com `bt_spp`);
- os `viagem.tsv` exportados;
- a linha `stack livre mínima: ...` do log;
- uma lista curta do que pareceu errado ("na 3ª mostrou 4", "bipou cedo", "ficou cinza a
  cada X s").

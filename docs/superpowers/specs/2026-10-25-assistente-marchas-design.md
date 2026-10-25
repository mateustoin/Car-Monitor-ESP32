# Car Monitor ESP32: assistente de troca de marchas (design + roadmap)

## Contexto

O Mateus tem um Renault Sandero Expression 1.6 8V 2013 manual e flex (motor K7M, câmbio JH3 de 5 marchas) e um adaptador ELM327 **Bluetooth clássico** que hoje usa no celular. Quer um ESP32 com tela, começando pelo **M5StickC Plus2**, que funcione como **assistente de direção** num carro antigo sem essas tecnologias. A ideia é mostrar o que os apps comuns não mostram. O objetivo é **eficiência e treino**: aprender o momento certo de cada troca até virar hábito.

O repositório está vazio (só README). O projeto cresce **em etapas**: cada etapa entrega uma função que funciona bem antes da próxima.

Ao aprovar este documento, o primeiro passo é salvá-lo em `docs/superpowers/specs/2026-10-25-assistente-marchas-design.md` e o banco de ideias em `docs/roadmap-ideias.md` (ambos commitados). Depois vem o plano de implementação (superpowers:writing-plans).

## Objetivos (Etapa 1)

1. **Indicador da marcha atual**, com as marchas aprendidas automaticamente.
2. **Assistente de troca focado em economia**: avisa quando subir, quando reduzir e **para qual marcha** reduzir.
3. Aviso por **tela e bipe**.

## Fora da Etapa 1 (vão para o roadmap)

Modos de ultrapassagem, subida carregado e freio motor. Consumo, saúde do carro, acelerômetro etc.

## O que não pode acontecer (anti-objetivos)

- Marcha piscando ou chutada durante a troca e com a embreagem pisada.
- Bipe repetitivo ou irritante.
- Mandar reduzir quando o carro desacelera com a injeção cortada.
- Código preso ao M5StickC Plus2 ou ao Bluetooth clássico.
- Distrair o motorista: a tela é para olhar de relance.

## Restrições (decisões do Mateus)

- **ESP-IDF 6.1**, seguindo as boas práticas da Espressif: componentes, Kconfig/menuconfig, gerenciador de componentes (idf_component.yml), FreeRTOS. Nada de Arduino.
- **Transporte genérico**: a primeira versão é Bluetooth clássico (SPP) no ESP32 original. A estrutura já fica pronta para **BLE** (ESP32-S3/C3/C6, que não têm Bluetooth clássico) e para trocar de adaptador. O BLE não precisa ser implementado agora.
- **Hardware desacoplado**: dá para trocar de ESP32 (S3, C3, C6), de **tela (tamanho ou modelo)** e de kit sem reescrever a lógica. A tela, o som e os botões ficam atrás de interfaces, e o layout se adapta à resolução.
- **Carro desacoplado**: começa no Sandero, mas a estrutura serve para outros carros manuais. Tudo o que é específico do carro mora num **perfil de veículo**, nunca no código.
- Tela: **fundo colorido** (escolha dele). Tela deitada (240×135). A cor do fundo inteiro comunica: **verde = ok, azul = suba, laranja = reduza**. A marcha fica grande no centro, com a marcha alvo ao lado (ex.: "→ 4"). A cor é lida pela visão periférica.

- **Alimentação**: USB **sempre ligado**, com energia mesmo com o carro desligado (resposta dele). O aparelho detecta o motor parado e entra em deep sleep sozinho, com estes critérios: RPM = 0 ou ELM sem resposta por ~N min **e** tensão (ATRV) abaixo de ~13 V, ou seja, alternador sem carregar. Acorda por botão ou por timer periódico, quando checa a tensão e o ELM. Isso evita descarregar a bateria do carro. O aprendizado de marchas é gravado no NVS assim que converge, nunca só no desligamento.
- **Gravação**: pelos **dois** caminhos (resposta dele). Serial USB quando houver notebook. Flash interna (partição LittleFS) quando não houver, com exportação pela serial depois.
- **PIDs e protocolo**: ainda **desconhecidos** (resposta dele). O firmware detecta tudo sozinho: `ATSP0`/`ATDPN` para o protocolo e bitmask 0100/0120/0140 para os PIDs. A Etapa 0 mostra na tela e no log o protocolo, os PIDs suportados e a taxa real de leitura. **Fallbacks**: se não houver 0x11 (acelerador), usa 0x04 (carga calculada); se não houver 0x0B (MAP), usa 0x04. Se o protocolo for K-line, a taxa pode cair para ~3–5 leituras/s. A agenda do poller e a histerese da marcha devem ser parametrizadas pela taxa medida.
- **Pareamento BT** (minha decisão): na primeira vez, procura por nome (padrão "OBDII", configurável) com PIN do Kconfig (padrão 1234) e salva o MAC no NVS. Dá para refazer pelo botão. Observação para o usuário: o SPP aceita só uma conexão, então o celular não pode estar conectado ao ELM ao mesmo tempo.

## Arquitetura (proposta minha)

Componentes ESP-IDF independentes, em `components/`:

| Componente | Responsabilidade |
|---|---|
| `obd_transport` | Interface genérica (vtable C): `connect`, `send(cmd)`, `read_until_prompt('>')`, `disconnect` e eventos de conexão. Implementações: `transport_bt_spp` (Etapa 1, via Kconfig só quando `SOC_BT_CLASSIC_SUPPORTED`), `transport_replay` (reproduz gravações, para testes) e, no futuro, `transport_ble` e `transport_wifi`. |
| `elm327` | Inicialização do ELM (ATZ, ATE0, ATL0, ATS0, ATH0, ATSP0 ou o protocolo detectado), parse das respostas, tratamento de `NO DATA`/`SEARCHING`/`?`, timeouts. Não conhece o transporte concreto. |
| `obd_poller` | Agenda de PIDs por prioridade. **Rápidos**: 0x0C RPM, 0x0D velocidade, 0x11 acelerador, 0x0B MAP (ou 0x04 carga). **Lentos**: 0x05 temperatura do motor e outros. Publica amostras com timestamp. Sabe que um ELM327 barato faz ~5–15 leituras/s. |
| `gear_engine` | Lógica pura em C, **sem dependência de hardware**: aprendizado das relações, detecção da marcha, sugestão de troca. Pode ser testado no host (Linux, `idf.py --preview set-target linux` ou Unity/CMock). |
| `ui` | Lógica de apresentação **independente de tela**: recebe o estado do `gear_engine` e desenha um layout que se adapta à resolução e à orientação informadas pela placa (posições e fontes proporcionais, nada de coordenadas fixas para 240×135). Fala só com as interfaces abstratas `display`, `buzzer` (ou "alerta sonoro") e `input` (botões). Biblioteca gráfica a critério do builder. Recomendação minha: **LVGL** sobre `esp_lcd`, porque já abstrai resolução, controlador e toque. |
| `board` / BSP | Única parte que conhece o hardware: controlador e tamanho da tela (ST7789V2 135×240 no Plus2), pinos, buzzer, botões, pino de "hold" da alimentação, IMU. Cada kit é um BSP selecionado via Kconfig e implementa as interfaces `display`, `buzzer`, `input` e `power`. **Trocar de ESP32, de tela (tamanho ou modelo) ou de kit = escrever ou escolher outro BSP**, sem tocar em `ui`, `gear_engine` ou `obd_*`. |
| `vehicle_profile` | **Perfil do carro**, para o projeto não ficar preso ao Sandero: número de marchas, faixa de RPM (marcha lenta, faixa econômica por carga, limite de "afogar", corte), combustível (flex, gasolina, etanol ou diesel, este com limiares diferentes), relações e pneu semente (opcionais) e PIDs preferidos ou fallbacks. O `gear_engine` lê tudo daqui e nunca tem números do Sandero no código. O perfil **"Sandero 1.6 8V K7M"** é o primeiro. Também existe um perfil "genérico manual" que funciona só com o aprendizado automático. Os perfis são definidos em arquivos de dados (ex.: JSON/Kconfig embutido) e o ativo fica salvo no NVS. Em cada carro, as relações aprendidas ficam associadas ao perfil, ou ao VIN (Mode 09) quando disponível. |
| `storage` | NVS: relações de marcha aprendidas, limiares ajustáveis, mute, endereço MAC do ELM pareado. |

Fluxo: transporte → elm327 → poller → (fila FreeRTOS) → gear_engine → ui.

## Lógica da Etapa 1 (proposta minha, com parâmetros ajustáveis)

**Marcha atual**
- Calcula `v/rpm` (km/h por 1000 RPM) continuamente.
- **Aprendizado automático**: nos primeiros km, agrupa amostras estáveis (velocidade > ~10 km/h, RPM estável, acelerador aplicado) em 5 clusters e salva no NVS. Dá para refazer pelo botão.
- **Valores semente:** o builder pode procurar as relações do JH3 e o pneu de fábrica para ter um chute inicial. Não achei esses valores confirmados na pesquisa.
- Quando nenhum cluster casa dentro da tolerância (embreagem pisada, ponto morto, troca em andamento), a tela mostra **"–"**.
- **Histerese**: a marcha só muda após leitura estável por ~300–500 ms.

**Subir marcha (economia)**
- Sugere subir quando o RPM passa do limite econômico **e** a marcha de cima deixaria o RPM ≥ ~1.500 (sem "afogar").
- O limite cresce com a carga (MAP ou acelerador): ~2.000–2.200 RPM com pé leve, até ~2.800 RPM com pé fundo. São chutes iniciais para o K7M.

**Reduzir marcha**
- Sugere reduzir quando o RPM fica < ~1.300 **com carga alta** (pedindo força).
- Mostra a marcha alvo: a que coloca o RPM na faixa útil (~2.000–2.500). Pode pular marchas.
- **Não** sugere reduzir em desaceleração sem acelerador, quando a injeção está cortada.

**Bipe**
- Um tom distinto para "suba" e outro para "reduza".
- Toca **uma vez por evento**, com intervalo mínimo entre bipes.
- Mute por botão.

**Robustez**
- Perda de conexão: tela indica e o aparelho reconecta sozinho.
- Amostras velhas (> ~1 s) não geram sugestão.

## Testes (proposta minha)

- `gear_engine` com testes unitários no host, usando dados sintéticos e gravações reais.
- Modo **gravação**: salva as amostras OBD numa viagem (log serial ou partição SPIFFS/LittleFS).
- `transport_replay` reproduz as gravações para ajustar os limiares sem sair com o carro.

## Roadmap (ordem proposta, a ajustar a cada etapa)

1. **Etapa 0, fundação**: projeto ESP-IDF 6.1, estrutura de componentes, BSP do M5StickC Plus2 (tela, bipe, botões), transporte BT SPP, inicialização do ELM327 e leitura de RPM e velocidade na tela. Gravação e replay.
2. **Etapa 1**: marcha atual com aprendizado automático e assistente de troca econômico (este documento).
3. **Etapa 2, economia visível**: indicador de "injeção cortada" (cut-off), consumo instantâneo estimado por MAP+IAT+RPM (speed-density, calibrado por tanqueada), custo da viagem em R$, custo da marcha lenta, comparação etanol × gasolina.
4. **Etapa 3, técnica**: detecção de embreagem patinando e meia-embreagem longa, alerta de motor "afogado", RPM alvo para punta-tacco na redução, nota da troca (suavidade).
5. **Etapa 4, saúde do carro**: tensão da bateria e do alternador (ATRV, queda na partida), limite de RPM com motor frio, tendência de fuel trim, leitura e limpeza de DTCs.
6. **Etapa 5, acelerômetro (IMU do Plus2)**: inclinação da via para antecipar a marcha na subida, nota de suavidade (frenadas, curvas, trancos).
7. **Etapa 6, modos além da economia**: ultrapassagem, subida carregado, freio motor na serra.
8. **Etapa 7, portabilidade**: transporte BLE, BSPs para ESP32-S3/C3/C6 e telas maiores, e novos perfis de veículo (com seleção de perfil pela própria interface).

## Banco de ideias

Ideias futuras, aguardando aprovação, ficam em [`docs/roadmap-ideias.md`](../../roadmap-ideias.md).

## Escopo do primeiro plano de implementação (minha decisão)

O primeiro plano cobre a **Etapa 0 inteira** e o **`gear_engine` da Etapa 1, testado no host com dados sintéticos**. O ajuste fino dos limiares e a validação no carro dependem de gravações reais e ficam para um segundo ciclo curto. As etapas seguintes ganham um plano próprio cada uma.

## Fica a critério do builder

Biblioteca gráfica, tons exatos do bipe, estrutura interna das tasks FreeRTOS, formato do log, algoritmo de clustering, detalhes de Kconfig.

## Verificação

- Testes do `gear_engine` passando no host (`idf.py` target linux ou Unity).
- Replay de uma gravação real reproduzindo as marchas e sugestões esperadas.
- No carro: a marcha correta aparece em todas as 5 marchas, "–" aparece com a embreagem pisada, o bipe toca uma vez ao cruzar o limite de subida e a cor da tela muda conforme o estado.
- Build para pelo menos um alvo sem BT clássico (ex.: esp32c3), confirmando que o projeto compila com o transporte SPP desligado via Kconfig.
- Teste no host do `gear_engine` com dois perfis diferentes (Sandero e um fictício de 6 marchas), provando que nenhum número do Sandero está fixo no código.
- A `ui` renderizada num display simulado de outra resolução (ex.: 320×240), ou num segundo BSP mínimo, sem mudar o código da `ui`.

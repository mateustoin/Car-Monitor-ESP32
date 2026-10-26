# Banco de ideias: Car Monitor ESP32

Ideias para etapas futuras. Status: `[ ]` pendente · `[x]` aprovada (mover para o roadmap no design) · ~~riscado~~ descartada.

As aprovadas entram nas etapas do roadmap em `docs/superpowers/specs/2026-10-25-assistente-marchas-design.md`.

Origem: **[B]** = brainstorm desta conversa · **[M]** = inspirada em produto de mercado · **[N]** = ideia nova surgida da pesquisa.

### Técnica de direção e marchas
- [ ] [B] Redução para ultrapassagem: marcha que dá a melhor força.
- [ ] [B] Modo subida carregado e modo freio motor na serra.
- [ ] [B] RPM alvo para casar a rotação na redução (punta-tacco).
- [ ] [B] Alerta de motor "afogado" (marcha alta e pé fundo).
- [ ] [B] Detecção de embreagem patinando e de meia-embreagem longa, com tendência de desgaste ao longo dos meses.
- [ ] [M] **Nota "estrelas" da viagem** em aceleração, troca de marcha e antecipação, no estilo do Renault Driving Eco² ([Renault e-guide](https://gb.e-guide.renault.com/eng/node/37)).
- [ ] [M] **Nota de antecipação**: penaliza frenada forte logo depois de acelerar, o que indica que dava para tirar o pé antes.
- [ ] [N] Modo "jogo": desafios ("10 trocas perfeitas seguidas", "viagem sem afogar") e recorde pessoal.
- [ ] [N] Relatório de hábitos: em que marcha você passa mais tempo, RPM médio das trocas, evolução semanal.

### Economia
- [ ] [B] Indicador de **injeção cortada** (cut-off).
- [ ] [B] Consumo instantâneo e médio estimado por MAP+IAT+RPM, calibrado pela tanqueada.
- [ ] [B] Custo da viagem em R$ e custo do tempo parado em marcha lenta.
- [ ] [B] Comparação **etanol × gasolina** com o consumo medido no carro.
- [ ] [M] **Vários trip computers**, como os 4 do ScanGauge: viagem atual, dia, tanque e total ([4x4 Australia](https://www.4x4australia.com.au/gear/ultragauge-obd2-scan-tool-tested)).
- [ ] [M] Consumo real comparado com o oficial do Inmetro/PBEV, no estilo do CarPrint ([Loyalty360](https://loyalty360.org/Content-Gallery/Daily-News/CarPrint-s-Eco-Driving-App-is-Engaging-and-Green)).
- [ ] [N] **Autonomia restante** do tanque, integrando o consumo desde a última tanqueada.
- [ ] [N] Conselheiro de abastecimento: você digita o preço dos dois combustíveis e ele diz qual compensa, pela razão real medida no seu carro em vez da regra dos 70%.
- [ ] [N] Sugestão de desligar o motor quando o carro fica parado em marcha lenta por mais de X segundos.

### Saúde e manutenção
- [ ] [B] Tensão da bateria e do alternador, queda na partida como aviso de bateria fraca.
- [ ] [B] Limite de RPM sugerido com o motor frio.
- [ ] [B] Tendência de fuel trim como alerta antes da luz de injeção.
- [ ] [M] **Varredura de DTCs durante a viagem** com alerta proativo, e leitura e limpeza de códigos, como no UltraGauge.
- [ ] [M] Status de prontidão das emissões (readiness), útil antes de vistoria.
- [ ] [M] PIDs estendidos da Renault (Mode 21/22): dados escondidos que o fabricante não expõe no OBD padrão ([Car Scanner](https://apps.apple.com/app/1259933623)).
- [ ] [N] **Monitor de bateria com o carro estacionado**: aproveita o USB sempre ligado, acorda de tempos em tempos para medir a tensão e avisa se ela cai durante a noite.
- [ ] [N] Lembrete de troca de óleo, correia dentada e velas por km e horas de motor contados pelo aparelho. A correia do K7M é item crítico.
- [ ] [N] Estatísticas de partida a frio: quantas por semana e tempo até aquecer.

### Acelerômetro (IMU do Plus2)
- [ ] [B] Inclinação da via para antecipar a marcha na subida.
- [ ] [B] Nota de suavidade: trancos na troca, frenadas e curvas.
- [ ] [M] **Teste de aceleração** 0–100 e retomadas (60–100, 80–120), com cronômetro ([Car Scanner](https://apps.apple.com/app/1259933623)).
- [ ] [N] **"Dinamômetro" estimado**: potência na roda calculada por massa × aceleração × velocidade numa puxada.
- [ ] [N] Alerta de movimento ou batida com o carro estacionado, usando o USB sempre ligado. Precisa de saída para o celular.

### Interface e conectividade
- [ ] [M] **Modo HUD**: tela espelhada para refletir no para-brisa, como nos HUDs OBD vendidos online ([cnx-software X5](https://cnx-software.com/?p=26923), [TechPP](https://techpp.com/?p=71535)).
- [ ] [M] Brilho automático (dia e noite por horário, ou sensor de luz em outro kit).
- [ ] [M] Alarme de excesso de velocidade configurável e lembrete de fadiga depois de X horas dirigindo.
- [ ] [M] Páginas de mostradores configuráveis pelo botão, como os 7 do UltraGauge.
- [ ] [N] Companheiro no celular por BLE ou Wi‑Fi: configuração do perfil, exportação de viagens e gráficos.
- [ ] [N] Página web local (o ESP32 como ponto de acesso Wi‑Fi) para baixar as gravações sem cabo.
- [ ] [N] Módulo GPS opcional: mapa da viagem e velocidade real, que ainda calibra o erro do velocímetro.
- [ ] [N] Avisos por voz curtos, com DAC ou I2S num kit com alto-falante, além do bipe.
- [ ] [N] Comparação "fantasma" do trajeto diário: mesmo percurso, hoje contra a melhor vez.


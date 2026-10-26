# Car-Monitor-ESP32
## Gravação de viagens (obd_recorder)

Console na UART (`idf.py monitor`, prompt `carmon> `):

- `rec on [serial|flash|both]` inicia a gravação (padrão: both); `rec off` para.
- `rec ls` lista `/littlefs/trip_NNN.tsv`, uso da flash e contadores; `rec dump <n>` imprime a viagem n; `rec clear` apaga todas.

Cada linha sai como `REC<TAB>t_ms<TAB>CMD<TAB>RESP`. Para obter o arquivo reproduzível no PC:

    idf.py monitor | grep '^REC' | cut -f2- > viagem.tsv

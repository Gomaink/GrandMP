# Alpha 0.3.1 — primeiro transporte de rede

O servidor é um processo separado do GTA. O Position Observer continua lendo o Hoodlum externamente; somente com `--connect 127.0.0.1:<porta>` ele envia posições por UDP. O `GrandMP.asi` não participa do transporte. A versão do protocolo (`1`) é independente da versão do produto definida em CMake.

## Protocolo v1

Cada datagrama contém exatamente uma mensagem. O cabeçalho de 8 bytes é: magic ASCII `GMP1` (4), versão `01` (1), tipo (1), comprimento do payload `uint16` big endian (2). Inteiros de payload são big endian; cada coordenada é o padrão de bits IEEE 754 binary32 de um `float`, codificado como `uint32` big endian. Nenhuma estrutura C++ é transmitida diretamente.

| Tipo | Valor | Payload | Total |
| --- | ---: | --- | ---: |
| Hello | 1 | nonce `uint64` | 16 bytes |
| Welcome | 2 | nonce `uint64`, session ID `uint64` | 24 bytes |
| PlayerPosition | 3 | session ID `uint64`, sequência `uint32`, X/Y/Z `float32` | 32 bytes |
| Disconnect | 4 | session ID `uint64` | 16 bytes |

Nonce, ID e sequência zero são inválidos. Cada eixo deve ser finito e estar em `[-100000, 100000]`. O codec rejeita versão, tipo, magic, tamanho ou campo inválido; o limite máximo é 32 bytes. O servidor associa o ID ao IPv4 e porta de origem, descarta sequência igual ou menor que a última aceita e aceita no máximo uma posição a cada 100 ms por sessão. A sequência v1 não dá volta: ao chegar ao limite `uint32`, uma nova sessão deve ser iniciada. O servidor mantém no máximo 64 sessões e remove uma após 10 s sem atividade aceita. Um Hello repetido com nonce e endpoint iguais reenvia o Welcome; o observer envia Hello a cada ~3 s para reencontrar um servidor reiniciado. Não há ACK para cada posição, retransmissão confiável, autenticação nem criptografia. **Este transporte experimental não é adequado para Internet ou servidores públicos.**

O servidor faz bind somente em `127.0.0.1`, porta 7777 por padrão ou `--port <1..65535>`. O observer aceita apenas o literal IPv4 de loopback. Não há abertura de firewall, acesso remoto ou transmissão automática no modo offline.

## Build e testes

```powershell
cmake -S . -B build/alpha031-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build/alpha031-win32 --config Debug
ctest --test-dir build/alpha031-win32 -C Debug --output-on-failure

cmake -S . -B build/alpha031-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/alpha031-x64 --config Debug
ctest --test-dir build/alpha031-x64 -C Debug --output-on-failure
```

Os testes usam tempo controlado na tabela de sessões e sockets UDP de loopback com dois clientes sintéticos. Eles não iniciam o GTA. O build Linux do servidor está pendente enquanto não houver toolchain Linux disponível neste ambiente.

## Smoke manual em `D:\Jogos\GrandMP-Smoke`

1. Em um PowerShell, inicie o servidor:

   ```powershell
   & 'D:\Programacao\Workspace\Applications\grandmp\build\alpha031-win32\server\Debug\grandmp_server.exe'
   ```

2. Em outro PowerShell, inicie o observer com rede explícita:

   ```powershell
   & 'D:\Programacao\Workspace\Applications\grandmp\build\alpha031-win32\client\windows\Debug\grandmp_position_observer.exe' --connect 127.0.0.1:7777
   ```

3. Inicie `D:\Jogos\GrandMP-Smoke\gta_sa.exe` normalmente. No menu, o observer deve identificar o Hoodlum e aguardar o jogador. Após carregar a partida, caminhe com CJ: o observer deve mostrar coordenadas, e o servidor deve registrar a sessão e posições X/Y/Z variando, com sequência crescente.
4. Feche o GTA e confirme `Processo encerrado` no observer e `desconectada` no servidor. O observer deve terminar sem travar. Reinicie o observer caso queira testar Ctrl+C com o GTA ainda aberto; esse cenário permaneceu pendente na Alpha 0.2.11.
5. Pressione Ctrl+C no PowerShell do servidor. Confirme `Servidor encerrado`.

Se não houver `Sessão ... conectada`, confira se as portas coincidem e se o observer mostrou `Sessao UDP GrandMP estabelecida`. Se o servidor reiniciar durante o teste, o observer deve obter um novo Welcome em até cerca de 3 s; confirme um novo ID de sessão e sequência começando em 1. Perdas UDP ocasionais são possíveis; ausência persistente de posições ou coordenadas divergentes deve ser registrada com as saídas dos dois terminais.

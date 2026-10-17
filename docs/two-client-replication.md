# Alpha 0.3.2 — replicacao de posicoes (dados, sem entidades no GTA)

## Escopo e limitacoes

Esta etapa preserva a Alpha 0.3.1: GrandMP.asi nao foi modificado, nenhum hook ou
WriteProcessMemory foi introduzido, e o observer continua opcional (`--connect`).
Os clientes compartilham somente estados de posicao; **nao** veem outros personagens
no mundo do GTA. O servidor so faz bind em `127.0.0.1`, e todos os participantes sao
experimentais, sem autenticacao, criptografia ou protecao contra cliente malicioso.
Nao exponha a porta UDP 7777 na Internet ou na LAN.

## Extensao compativel do protocolo UDP v1

O cabecalho `GMP1`, versao 1, e os tipos 1-4 permanecem **byte a byte iguais**.
Clientes 0.3.1 ignoram novos tipos recebidos (sem suporte a jogadores remotos).
Os novos tipos sao:

| Tipo | Decimal | Payload big-endian | Tamanho total |
| --- | ---: | --- | ---: |
| RemoteJoin | 5 | `player_id` uint64 | 16 |
| RemotePosition | 6 | `player_id` uint64, `sequence` uint32, XYZ IEEE754 binary32 | 32 |
| RemoteLeave | 7 | `player_id` uint64 | 16 |
| SnapshotRequest | 8 | `session_id` uint64 | 16 |

`player_id` corresponde ao ID de sessao gerado pelo servidor, nunca ao PID nem a
um ID escolhido pelo cliente. Nao ha transmissao de estruturas C++ diretamente.
Coordenadas finitas em `[-100000,+100000]` por eixo, ID e sequencia > 0.

## Fluxos

1. Novo Hello recebe Welcome e um snapshot inicial (RemoteJoin e, se disponivel,
   RemotePosition para cada sessao existente). Outros clientes recebem RemoteJoin.
2. PlayerPosition validado e retransmitido aos demais como RemotePosition, nunca
   ao remetente. Tabela de sessoes possui ate 64 participantes e ultimo estado aceito.
3. Disconnect ou timeout envia RemoteLeave aos demais. Um Hello com nonce novo do
   mesmo endpoint gera saida da sessao antiga e entrada da nova.
4. O observer e o cliente sintetico enviam SnapshotRequest a cada ~3 segundos.
   O servidor valida sessao/endpoint e limita pedidos a um por segundo por sessao.
   Isso repara perdas de Join e ultima Position, sem garantir entrega ordenada.
5. O cliente remoto ignora sequencia antiga, aceita repeticao da ultima sequencia
   como refresh de presenca, remove participantes sem atualizacoes apos 8 segundos
   e ignora pacotes atrasados de jogadores removidos durante 30 segundos.
6. Reconnect / mudanca do session ID limpa o roster local para evitar fantasmas.

O snapshot e **uma serie de datagramas**, nao uma transacao atomica. A ausencia
simultanea de eventos e de refresh pode fazer um participante desaparecer
provisoriamente. Essa estrategia simples e suficiente para o teste localhost,
**nao** para sincronizacao de producao com perdas significativas.

## Compilacao Windows (Visual Studio 2026)

Na raiz do projeto, com o Windows PowerShell:

```powershell
cmake -S . -B build/alpha032-win32 -G "Visual Studio 18 2026" -A Win32
cmake --build build/alpha032-win32 --config Debug
ctest --test-dir build/alpha032-win32 -C Debug --output-on-failure

cmake -S . -B build/alpha032-x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/alpha032-x64 --config Debug
ctest --test-dir build/alpha032-x64 -C Debug --output-on-failure
```

O Windows precisa compilar os novos fontes e executar CTest. No ambiente Linux
foi validado o core, servidor e cliente sintetico; nenhuma compilacao MSVC foi
executada durante a revisao por chat.

## Smoke manual: GTA real + segundo jogador sintetico

**Terminal 1 — servidor:**

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha032-win32\server\Debug\grandmp_server.exe'
```

**Terminal 2 — jogador B (nao precisa abrir outro GTA):**

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha032-win32\client\synthetic\Debug\grandmp_synthetic_client.exe'
```

**Terminal 3 — GTA real (jogador A):**

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha032-win32\client\windows\Debug\grandmp_position_observer.exe' --connect 127.0.0.1:7777
```

Abra `D:\Jogos\GrandMP-Smoke\gta_sa.exe` normalmente e entre na partida.

Verificar:

- O sintetico exibe `Remoto <id> ... X=<coordenada real do CJ>` quando A se move.
- O observer exibe `Jogador remoto <id> entrou` e `Remoto <id> ... X=100.*`
  (a posicao do sintetico). O GTA **nao** desenha este segundo jogador.
- Ao apertar Ctrl+C **apenas no Terminal 2**, o observer exibe `saiu`;
  o servidor registra `desconectada`.
- Reiniciar o Terminal 2 cria outro ID; observer percebe nova entrada.
- Fechar GTA pelo menu causa desconexao do Terminal 3; o sintetico detecta a saida.
- Finalmente, Ctrl+C no servidor o encerra e libera a porta 7777.

Este smoke exige VS 2026 e deve ser feito pelo usuario. Nao copiar nenhum ASI
para a instalacao GTA. Se houver falhas, capturar os trechos finais dos tres
terminais, os resultados do CTest e o erro de compilacao exato.

## Testes automaticos

- `grandmp_protocol_tests`: roundtrip dos tipos 1-8 e rejeicao de campos invalidos.
- `grandmp_remote_roster_tests`: entrada, saida, tombstone, sequencia, TTL, limites.
- `grandmp_server_replication_state_tests`: ultimo estado, re-hello, endpoints,
  limite de SnapshotRequest e timeout (relogio simulado).
- `replication_loopback.py` (CTest quando Python 3 disponivel): inicia **servidor real**
  e testa tres sockets UDP: Welcome, snapshot inicial, snapshots sob demanda,
  posicoes retransmitidas, IDs diferentes, saida, duplicata e endpoint divergente.
- O teste Linux manual adicional executou **dois processos sinteticos reais**
  e confirmou entrada, atualizacoes mutuas e encerramento com SIGINT.

Uma integracao fisica com GTA no Windows e testes MSVC ainda sao obrigatorios.

Nota de consolidação (2026-10-13): o parágrafo acima registra a pendência do handoff original. Posteriormente, o usuário confirmou a validação física das Alphas 0.3.1–0.3.3 e seus builds MSVC. O estado consolidado e os limites atuais estão em [ROADMAP.md](ROADMAP.md); essa confirmação não equivale a jogador remoto visível no GTA.

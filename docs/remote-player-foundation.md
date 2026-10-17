# Alpha 0.3.3 — Remote Player Foundation (apresentacao portatil)

## Objetivo e criterio de GO

A Alpha 0.3.2 validou no Windows, com CJ real + cliente sintetico, a replicacao
bidirecional e a propagacao de entrada/saida. Esta etapa **nao** tenta criar peds
no GTA. Ela constrói o lado portatil do ciclo de vida de jogadores remotos e um
contrato de apresentacao preparado para adaptadores Windows e Android futuros.

Nenhuma alteracao em GrandMP.asi, GTA, enderecos de memoria, offsets, RenderWare,
protocolo UDP ou servidor. Apenas leitura externa `ReadProcessMemory` existente.

## Componentes

- `RemoteRoster`: continua sendo a **fonte de verdade** de IDs, posicoes,
  sequencias, limites (63 remotos), expiracao e tombstones. Novo metodo `forEach`
  const permite a um consumidor visitar entradas sem alocar snapshots ou expor
  a representacao interna.
- `RemotePlayerScene`: mantem uma representacao visual por ID com posicao
  corrente, destino e inicio da transicao. So cria representacoes depois de
  receber uma posicao valida; um `RemoteJoin` sozinho nao basta.
- `IRemotePlayerSceneSink`: contrato `spawn(pose)`, `move(pose)` e `despawn(id)`.
  O core nao cria/gerencia peds, texturas ou objetos de engine. Implementadores
  devem executar callbacks no **thread autorizado da plataforma** e nao lancar
  excecoes, reter referencias ou bloquear.
- `ObserverNetwork::setRemoteSceneSink`: permite plugar um sink de diagnostico
  opcional. Sem sink, modo 0.3.2 permanece inalterado. Antes de limpar o roster
  por reconexao/timeout ou ao parar, o observer remove as representacoes.

### Interpolacao e limites

- Lerp linear por eixo em 180 ms; trabalha apenas entre amostras confirmadas,
  **sem extrapolacao** de velocidade ou posicao.
- Se houver novo destino durante uma transicao, parte da pose atual e evita
  saltos por reinicio incorreto.
- Saltos maiores que 35 unidades GTA sao apresentados imediatamente, como
  teleport/respawn; esse limiar e provisório ate haver dados de gameplay.
- Seq repetida / snapshot sem atualizacao nao reinicia interpolacao.
- Cada `synchronize()` deve ser chamado periodicamente na thread que pertence
  ao sink. Para movimento visual suave, o futuro adaptador devera sincronizar
  na cadencia segura do **frame**, nao na cadencia de polling de 200 ms.
- Sem orientacao, animacoes, modelo ou visibilidade/streaming espacial neste
  slice. O protocolo atual transmite somente XYZ.
- Nao ha tentativa de sincronizacao de tempo de rede ou clock remoto; a
  interpolacao inicia ao observar uma nova sequencia validada.

## Diagnostico Windows

Foi acrescentada a opcao do observer:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha033-win32\client\windows\Debug\grandmp_position_observer.exe' --connect 127.0.0.1:7777 --remote-scene-preview
```

A opcao exige `--connect` e produz linhas como:

```text
[scene] spawn remoto <id> em 100.100 200.000 30.000
[scene] pose remoto <id> seq=25 X=102.400 Y=200.000 Z=30.000
[scene] despawn remoto <id>
```

**Esses eventos sao somente texto no console, nao personagens visiveis no GTA.**
O observer deve continuar exibindo os logs de replicacao 0.3.2, e a saida de
um cliente/remoto deve gerar `despawn` sem interromper o CJ.

### Passo a passo de smoke

1. Build Win32 com MSVC e `ctest` completo; tambem testar x64 (core/server).
2. Executar servidor `build/alpha033-win32/server/Debug/grandmp_server.exe`.
3. Executar `build/alpha033-win32/client/synthetic/Debug/grandmp_synthetic_client.exe`.
4. Executar observer acima, abrir GTA, entrar na partida.
5. Confirmar `spawn` e linhas amostradas de `pose` do remoto.
6. Ctrl+C no sintetico; observer deve mostrar `Jogador remoto ... saiu` e
   `[scene] despawn remoto ...`; CJ deve continuar sendo lido.
7. Encerrar GTA pelo menu; observer deve sair sem `Ctrl+C`.
8. Ctrl+C no servidor e verificar porta UDP 7777 livre.

## GO / NO-GO

GO para a **foundation de apresentacao** depois dos testes Win32 e smoke de
entrada/saida. Nao declarar GO para `ped` nativo, hook, injecao ou representacao
visual dentro do GTA. Isso depende de um ponto de execucao GTA comprovadamente
seguro, que permanece nao homologado depois das investigacoes da Alpha 0.2.

O servidor permanece ligado exclusivamente em 127.0.0.1, sem autenticacao.
Nao exponha a porta em LAN/Internet.

# Alpha 0.3.6 / Slice 2 — Native Scene Bridge

## Estado e escopo

Esta slice conecta a replicação remota existente a uma fila consumida no owner thread, usando somente um backend simulado em processo independente. Ela não instala callbacks no GTA, não altera o GrandMP.asi homologado e não cria peds. A integração nativa do motor continua NO-GO conforme os gates de N1 em [ROADMAP.md](ROADMAP.md).

A validação histórica da Slice 1 segue registrada em [native-counter-prototype.md](native-counter-prototype.md): 117/117 CTest Win32 e 107/107 x64. A validação deste estado do workspace está registrada ao final após executar novamente toda a suíte.

## Fluxo

```text
GrandMP Server UDP v1
        ↓
ObserverNetwork::tick (host independente)
        ↓
RemoteRoster → RemotePlayerScene
        ↓ spawn / move / despawn
QueuedRemotePlayerAdapter (fila limitada, geração, coalescing)
        ↓ pump no owner thread, depois do tick de rede
SimulatedEntityBackend (registro observável de operações)
```

`ObserverNetwork` continua responsável pelo transporte, validação de mensagens e sincronização do roster/cena. Seu sink só chama métodos produtores do adapter. `pump()` é invocado depois do processamento de rede no loop principal e é a única etapa que chama o backend. O host fixa o owner thread antes de iniciar o recebimento. O backend de teste também rejeita qualquer chamada durante a fase de rede ou fora do thread fixado.

Não há mutex do adapter mantido durante `available`, `create`, `update`, `remove` ou `removeAll`. O adapter reserva um comando como in-flight sob lock, executa a chamada externa sem o lock e confirma o resultado sob lock. `pumping_` impede reentrância do consumidor. Uma falha transitória mantém/reagenda a operação segundo a política existente; falhas persistentes entram em cleanup fail-closed.

## Sessões, ordenação e cleanup

- `RemoteRoster` continua descartando sequências antigas/duplicadas e aplica seus limites de jogadores, timeout e tombstones.
- `RemotePlayerScene` continua sendo a única fonte de spawn, poses interpoladas/teleport e despawn.
- `ObserverNetwork` notifica `resetSession()` no sink ao limpar a sessão, antes de sincronizar poses do novo `Welcome`. O adapter abre sua geração nessa notificação; isso conserva o spawn mesmo se Welcome e snapshot chegarem no mesmo tick. IDs podem ser reutilizados sem herdar estado materializado da sessão anterior.
- Spawn em seguida de despawn conserva a ordem. Reutilização de ID durante `create` ou `remove` in-flight resulta em `remove` seguido de `create`, e não em update de entidade ausente.
- O host encerra primeiro a rede, fecha produtores, solicita shutdown e chama cleanup no owner thread antes de destruir o backend.
- O shutdown do adapter é idempotente. Se o backend estiver indisponível, o cleanup aguarda uma próxima chamada de `pump()`; erro persistente permanece explícito em `cleanup_failed` e não é tratado como limpeza concluída.

O protocolo atual não carrega uma geração de sessão em cada mensagem de jogador. A troca de sessão reseta o estado recebido, mas não pode distinguir perfeitamente um datagrama atrasado da sessão anterior caso ele chegue depois do novo `Welcome`. Essa limitação do protocolo permanece documentada e não foi alterada nesta slice.

## Execução independente

Em três terminais PowerShell, sem iniciar GTA:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\server\Debug\grandmp_server.exe' --port 7777
```

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\grandmp_remote_scene_host.exe' --port 7777
```

Inicie em dois outros terminais:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\synthetic\Debug\grandmp_synthetic_client.exe' --port 7777
```

O host deve imprimir IDs remotos em `[backend] spawn`, poses amostradas em `[backend] pose`, saídas em `[backend] despawn` e terminar com `cleanup=ok affinity=ok` após Ctrl+C. O backend é em memória e não desenha entidades.

## Testes de integração

- `remote_scene_adapter_pipeline`: harness loopback WinSock controla mensagens e percorre o `ObserverNetwork`, roster, cena, adapter e backend. Inclui dois IDs, sequência repetida/antiga, remoção, troca de sessão com reuso de ID, retry de criação, corrida de produtor/shutdown e verificação de owner thread.
- `server_to_remote_scene_adapter`: inicia o servidor GrandMP real, o host e dois `grandmp_synthetic_client` reais. Confirma spawn de ambos, poses, despawn por saída e cleanup final. É um teste de loopback automatizado, não smoke no GTA.
- `remote_adapter_backend_unlocked`: o backend aguarda um produtor inserir movimento durante `create`; sucesso demonstra que chamadas ao backend não retêm o mutex da fila.
- `remote_adapter_reuse_during_remove` e `remote_adapter_reuse_after_create_despawn`: forçam pontos de sincronização no backend e validam a reutilização concorrente de ID.
- Os testes legados do protocolo, servidor, observer, cenas e adapter permanecem na suíte.

## Relação com o roadmap e limites

Esta é uma ponte demonstrável de replicação até um backend consumido no owner thread do host; não é uma ponte de processo para o ASI. A recomendação de N2 no roadmap continua sendo transportar comandos por IPC local limitado até o módulo residente, sem executar backend na thread do pipe. Esse IPC depende de N1 e está fora desta slice.

O host não comprova thread-affinity do motor, loader lock, residência do ASI, patch ownership, instalação concorrente, ABI de criação de entidades ou segurança do ciclo de vida no GTA. Um futuro backend GTA ainda exige o callback validado e gates de N1/N2/N3. Portanto, o resultado é **GO para o bridge simulado independente; NO-GO para smoke no GTA e criação de peds**.

## Validação executada

Build Debug com Visual Studio 18 2026 / MSVC 19.51.36260 concluído em Win32 e x64. CTest completo: **122/122 Win32** e **110/110 x64**. O teste `server_to_remote_scene_adapter` passou com o servidor GrandMP real, dois `grandmp_synthetic_client` e o host independente. `git diff --check` foi executado; os arquivos não rastreados também foram verificados quanto a whitespace final. O smoke executado foi loopback automatizado, não GTA.

No fechamento pela Slice 3, as regressões foram ampliadas para dois spawns durante remoção in-flight e Welcome+snapshot no mesmo tick. Ambos reproduziram falhas antes das correções; coalescing preserva a recriação e a notificação síncrona conserva a fronteira da sessão. O backend do host mantém create idempotente e o harness fecha sockets antes do Winsock em todos os retornos. Resultados e revisão atualizados: [native-game-thread-integration.md](native-game-thread-integration.md).

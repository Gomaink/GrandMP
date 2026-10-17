# Alpha 0.3.6 / Slice 1 — First Native Callback Prototype

## Decisão e relação com o roadmap

GO para o protótipo contador isolado; NO-GO para instalação e smoke de callback no GTA. Este slice implementa parte de N1 em ROADMAP.md, sem fechar a Alpha 0.3.6: callback físico, budget p99, soak de 30 minutos, 10 ciclos de partida e ponte N2 continuam pendentes. Não há entidades nem conexão com RemotePlayerAdapter.

A barreira é específica: ninguém demonstrou exclusão entre execução do CALL e escrita dos seus cinco bytes, nem propriedade exclusiva frente a outros modificadores. O artefato destinado ao GTA não contém instalador de patch. Aprovação da identidade e dos bytes continua terminando em installation_unavailable. Não há opção de configuração capaz de ultrapassar essa barreira.

## Evidências reutilizadas

- Hoodlum: SHA-256 A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26, 14.383.616 bytes, PE32/i386, base 0x00400000.
- Capturas físicas de 20261012-223706: CALL em 0x53E981, bytes E8 5A D5 FF FF, destino 0x53BEE0. A primeira fase não foi comprovadamente menu. TID coincidente não certifica afinidade para APIs do motor.
- Captura de 20261013-002726: LoaderLock livre no instante observado de InitializeASI, conforme relatório reprocessado. Não é contrato universal de ausência do lock nem prova de instalação segura.
- Bootstrap 0.3.5: InitializeASI solicita; uma chamada explícita posterior faz o trabalho diferido. O loader não fornece ao plugin um ponto comprovado de quiescência do call-site.

## Auditoria do mecanismo candidato

Fonte analisada localmente: plugin-sdk revisão `15f15b60bbf74c106e1b496ff92c98764abf4605`, já preservada em pesquisa ignorada. Não se incorporou código ou biblioteca do plugin-sdk ao novo artefato.

[Events.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/Events.h), linha 404: `CdeclEvent`, assinatura `void()`, prioridade AFTER, H_CALL em 0x53E981. [EventList.h](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/shared/EventList.h): Add chama Patch antes de emplace_back; Patch publica bPatched antes de concluir PatchAll. Dispatcher chama original antes de handlers AFTER. Add/Remove/iteração não estabelecem sincronização externa.

[injector.hpp](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/injector/injector.hpp): MakeCALL escreve opcode E8 e rel32 separadamente. VirtualProtect não estabelece exclusão de execução. [hooking.hpp](https://github.com/DK22Pac/plugin-sdk/blob/15f15b60bbf74c106e1b496ff92c98764abf4605/injector/hooking.hpp): scoped_basic pode restaurar bytes no destrutor sem certificar ownership concorrente. Remove só apaga entradas; não restaura CALL e não prova quiescência.

Licença consultada: aviso zlib-style do plugin-sdk/injector na revisão fixa, registrado nas pesquisas anteriores. Análise não introduz dependência redistribuída. Não existem arquivos GTA incorporados.

## Menor alteração técnica necessária

Antes de habilitar um instalador real, demonstrar um ponto cooperativo na thread proprietária, anterior ao primeiro despacho, com ordering que exclua executores e terceiros durante validação/escrita/publicação. Alternativamente, avaliar uma operação transacional auditada que cubra esses mesmos riscos. Não basta adicionar mutex do GrandMP, booleano de quiescência, PIN, comparação de bytes ou FlushInstructionCache.

O mecanismo candidato continua sendo encaminhamento do CALL original seguido do contador AFTER. Nenhuma rotina em 0x53BEE0 é chamada pelo laboratório: o host invoca sua própria função `__cdecl` conhecida antes do contador. Isso exercita encaminhamento cooperativo no host, não demonstra ABI, registradores ou wrapper para o GTA.

## Gates para eventual smoke, sem autorização neste slice

1. Fingerprint/PE/base e bytes verificados imediatamente sob a exclusão do instalador.
2. Ponto de instalação fora do loader lock e sem execução concorrente de instrução parcial; recusa de terceiros e falha parcial conservadora.
3. Estado e código residentes antes da publicação; não hot-unload; nenhum destrutor restaura memória executável.
4. Original encaminhado exatamente uma vez, inclusive diagnóstico desativado; callback somente contador lock-free, sem log, mutex ou engine.
5. Thread/ciclo observado com critério explícito; autorização manual separada para instalação isolada sem outros ASIs desconhecidos.

Se aprovados futuramente: confirmar menu, loading, partida e saída; observar crescimento do contador fora do callback; encerrar normalmente. Rollback somente com processo encerrado: remover experimental/configuração. Não remover handler como tentativa de tornar unload seguro. Este documento não fornece comando de instalação ou execução do GTA, pois o gate permanece NO-GO.

## Código e contrato efetivamente implementados

- `client/windows/tools/native_counter_gate.hpp`: preflight puro de opt-in, PE/fingerprint e exatamente cinco bytes esperados. Executável correto continua bloqueado. Recebe dados do chamador para testes; o ASI inerte não faz hashing nem coleta automática de memória do jogo.
- `native_counter_experimental.cpp` / `native_counter_experimental.def`: target Win32 `GrandMPNativeCounterExperimental.asi`. InitializeASI apenas solicita uma vez; exports de leitura e status somente. Não contém prepare, dispatch, PIN, writer ou instalação GTA. Estado/contador atômicos não têm destrutor de patch.
- O mesmo cpp, compilado exclusivamente com `GMP_NATIVE_COUNTER_HOST_TEST` e `native_counter_lab.def`, gera `GrandMPNativeCounterLab.dll`. Uma chamada explícita do host depois de LoadLibrary adquire módulo por endereço, confirma identidade e faz PIN uma única vez. PIN é irreversível até o término do processo; não é política de hot-unload nem autorização para patch.
- `native_counter_host.cpp`: fonte específica e cooperativa publica um único callback; rotina original própria é executada uma vez por update antes do callback. Shutdown fecha publicação, drena chamadas em andamento e só então desativa logicamente. Não contém patch em instruções nem chama código GTA. Mutexes e espera pertencem ao host, fora do contador.

O callback tem exatamente `fetch_add(1, memory_order_relaxed)` e exige atomic uint64_t lock-free por static_assert. O dispatcher faz o gate enabled fora dele. Preparação e cancelamento concorrentes são serializados apenas no laboratório; cancelamento impede reativação. A desativação isolada pode coexistir com uma chamada que já passou pelo gate: não promete quiescência. O contrato usa fechamento/drenagem da fonte antes de considerar o contador estabilizado. Módulo PIN e estado estático continuam residentes; não há FreeLibrary da residência nem cleanup de código em DllMain.

Não existe instalação de hook neste slice, mesmo no host. Instalação testada significa publicação cooperativa do callback; os testes de bytes são testes reais do preflight conservador, não de patch/trampoline. Falha parcial exercita falha antes do PIN e antes da publicação; falha de VirtualProtect/patch não é alegada, porque não há writer.

## Validação executada

Builds Debug com Visual Studio 18 2026 / MSVC 19.51.36260 concluídos nas duas arquiteturas. CTest: **117/117 Win32** e **107/107 x64**. Os testes legados foram preservados; 11 novos cenários foram adicionados ao Win32 e 10 ao x64. Os cenários `shutdown_race` testam cancelamento concorrente à preparação; `inert_asi` verifica que o ASI não exporta as funções do laboratório e pode ser descarregado quando nada foi publicado. Cada CTest tem timeout de 15 segundos.

Comandos reproduzíveis:

```powershell
cmake -S . -B build/alpha036-win32 -G 'Visual Studio 18 2026' -A Win32
cmake --build build/alpha036-win32 --config Debug --parallel 4
ctest --test-dir build/alpha036-win32 -C Debug --output-on-failure
cmake -S . -B build/alpha036-x64 -G 'Visual Studio 18 2026' -A x64
cmake --build build/alpha036-x64 --config Debug --parallel 4
ctest --test-dir build/alpha036-x64 -C Debug --output-on-failure
```

O host pode ser executado manualmente sem GTA:

```powershell
& 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\grandmp_native_counter_host.exe' 'D:\Programacao\Workspace\Applications\grandmp\build\alpha036-win32\client\windows\Debug\GrandMPNativeCounterLab.dll' concurrent
```

Architect: GO para host e ASI inerte; NO-GO para instalação no GTA. Windows native: o CALL é escrito em partes pelo mecanismo auditado e falta exclusão de executores; o host só comprova fonte cooperativa. Reviewer independente: sem bloqueadores para host/ASI inerte; ABI do wrapper GTA, ownership do patch e instalação concorrente no jogo continuam sem comprovação. Nenhum agente recomenda smoke físico.

Resultado do slice: **GO para o protótipo isolado; NO-GO para smoke de callback no GTA**. A Alpha 0.3.6 continua aberta, pois o critério de callback físico, budget/soak e a ponte N2 do roadmap não foram demonstrados.

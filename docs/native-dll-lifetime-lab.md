# Alpha 0.3.5 — Real DLL Lifetime Laboratory (Slice 2)

## Isolamento

O laboratório carrega uma DLL de teste própria com `LoadLibraryW`, resolve uma interface C versionada via `GetProcAddress` e a descarrega com `FreeLibrary`. Seus alvos são `grandmp_callback_lifetime_module.dll` e `grandmp_callback_dll_lifetime_host.exe`. Eles são independentes de `GrandMP.asi` e do GTA; não há hook, patch, injeção ou escrita em processo externo. O `DllMain` da DLL retorna imediatamente. Toda alocação, criação de callbacks e inicialização ocorre pela exportação `GmpLabGetApi`/`initialize` depois que `LoadLibraryW` retornou.

O host mantém o gate das entradas na tabela de exports. Cada chamada `invoke()` adquire uma contagem ativa antes de copiar/usar a tabela e a libera depois do retorno da exportação. A fonte de atualização é um conjunto de threads criado pelo host; ela chama `tick()` e não pertence à DLL. O lifecycle dentro da DLL registra o callback de teste, despacha-o e fornece o estado de cleanup. A inicialização da DLL usa um `atomic_flag` para aceitar no máximo uma chamada concorrente e publica o estado por ponteiro atômico; o teste `concurrent_initialize` exige exatamente um vencedor.

## Sequência de unload testada

1. Parar a fonte de ticks e juntar todas as threads que poderiam entrar na DLL.
2. No mesmo gate, exigir `sourceJoined`, nenhuma chamada API ativa e nenhuma tentativa de unload concorrente; então reivindicar uma única tentativa e fechar novas entradas.
3. Solicitar shutdown, confirmar a quiescência da fonte e aguardar os callbacks/cleanup.
4. Exigir que a DLL informe `unloadSafe`, coletar métricas e destruir seu estado.
5. Limpar a tabela de funções no host e chamar `FreeLibrary`.
6. Confirmar por `GetModuleHandleW` que o módulo deixou de estar carregado.

Qualquer falha após fechar a admissão deixa o `HMODULE` retido pelo host até o término do processo de teste; antes de retornar, o host salva métricas para comprovar se cleanup foi executado. O destrutor do host não chama `FreeLibrary` como fallback. Tentativas concorrentes são serializadas pelo claim sob mutex. Uma chamada exportada direta bloqueada dentro do callback também mantém a contagem ativa: o cenário `direct_active` testa essa condição sem dispatcher em execução.

Os exports pressupõem que o host respeite esse contrato: não chamar funções da API fora do gate, não iniciar ticks/metrics antes de `initialize` retornar e não destruir o estado enquanto outro chamador possa entrar. A publicação do ponteiro é atômica e chamadas antecipadas veem `nullptr`, mas a DLL de laboratório não é uma biblioteca genérica thread-safe para callers arbitrários durante `destroy`; o host impede essa condição com `activeCalls` e join. `GetModuleHandleW` verifica o estado de carregamento do módulo no próprio processo; não prova que ponteiros guardados externamente deixaram de existir, por isso o host apaga sua tabela antes do unload e não a expõe.

## Casos automatizados

CTest executa cenários separados, cada um num processo host novo:

- `normal`: dois dispatchers, callback ativo, shutdown/join/cleanup/unload e contagens equilibradas.
- `active`: callback bloqueado; a tentativa real `tryUnload()` deve ser recusada e o módulo deve continuar carregado.
- `direct_active`: export `tick()` bloqueada numa chamada direta, sem dispatcher; unload deve ser recusado por `activeCalls`.
- `shutdown_active`: pedir shutdown enquanto callback está bloqueado, liberá-lo, juntar a fonte e descarregar.
- `init_failure`: falha parcial com rollback bem-sucedido permite shutdown e unload.
- `rollback_failure` e `cleanup_failure`: falha de rollback/cleanup mantém o módulo carregado e falha o gate.
- `reentrant`: o callback reentra em `tick`; o controller recusa a reentrada e o módulo depois descarrega normalmente.
- `cycles`: 40 ciclos de load/initialize/dispatch/shutdown/unload no mesmo processo.
- `concurrent_unload`: dois threads disputam `tryUnload`; exatamente um pode obter o claim e descarregar.
- `concurrent_initialize`: dois threads disputam a inicialização; apenas um registra runtime e o outro recebe falha sem sobrescrever estado.

Cada teste CTest tem timeout de 15 segundos. A saída do host é verificada pelo código de saída real do processo. Os cenários de falha intencional verificam o módulo ainda carregado antes do encerramento normal do processo, quando o Windows encerra os módulos restantes.

Execução isolada Win32:

```powershell
ctest --test-dir build/alpha035-win32 -C Debug -R callback_dll_lifetime --output-on-failure
```

Execução isolada x64:

```powershell
ctest --test-dir build/alpha035-x64 -C Debug -R callback_dll_lifetime --output-on-failure
```

## Garantias e limites

O laboratório demonstra no host controlado que: uma DLL real pode registrar e executar código callback; o host impede o unload enquanto uma chamada exportada está ativa ou a fonte de dispatch ainda não foi juntada; depois da quiescência, o estado pode ser destruído e `FreeLibrary` remove o módulo; falhas de cleanup não são confundidas com autorização de unload.

Isso não prova segurança contra um terceiro que chame `FreeLibrary` diretamente, que retenha uma cópia privada de ponteiro de função ou ignore o gate do host. Tampouco demonstra o loader lock do Ultimate ASI Loader, a residência do ASI no GTA, a propriedade/restauração do patch de `Events::gameProcessEvent`, a ausência de execução concorrente do call-site, thread-affinity do motor ou encerramento do GTA. O host controla e junta sua própria fonte; o GTA e plugins de terceiros ainda não estão sob esse controle demonstrado.

**Decisão:** GO apenas para o ciclo LoadLibrary/dispatch/quiesce/FreeLibrary desta DLL descartável e deste host cooperativo. Continua **NO-GO** instalar `gameProcessEvent` ou qualquer callback no Hoodlum com base neste laboratório.

O próximo passo concreto, após revisão e autorização separada, seria uma investigação curta no loader/ASI real que demonstre ponto de inicialização fora do loader lock e política de residência observável. Mesmo isso não resolve propriedade/concorrência do patch; esses gates precisam de evidência independente antes de um callback contador no GTA.

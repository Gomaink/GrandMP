# Alpha 0.2 — integração nativa Windows

Para a análise específica do loader 7.7.0 e do ciclo de vida sem patch da Alpha 0.2.4, ver [hook-lifecycle.md](hook-lifecycle.md). A revisão corrente do repositório mencionada abaixo não substitui a tag 7.7.0 analisada nesse documento.

A política experimental de residência da Alpha 0.2.5 está em [process-residency.md](process-residency.md). O PIN foi exercitado somente em processo CTest isolado e permanece desabilitado no ASI de produção; não há hook nem leitor de jogador.

## Fontes e limites verificados

Consulta em 2026-10-11 aos ramos indicados abaixo. Os HEADs consultados por `git ls-remote` foram [plugin-sdk `15f15b60bbf74c106e1b496ff92c98764abf4605`](https://github.com/DK22Pac/plugin-sdk/tree/15f15b60bbf74c106e1b496ff92c98764abf4605), [gta-reversed `c85e5dcd9b7aa088e9e7a80f647d98bcd14a140f`](https://github.com/gta-reversed/gta-reversed/tree/c85e5dcd9b7aa088e9e7a80f647d98bcd14a140f), [SAEncyclopedia `b873c48bdb372e87030ba708ac68fdabe13b9716`](https://github.com/TsyVM/SAEncyclopedia/tree/b873c48bdb372e87030ba708ac68fdabe13b9716), [sa-sdk `99787a3c92019de04d61d68bc5e1271233ed2e41`](https://github.com/TsyVM/sa-sdk/tree/99787a3c92019de04d61d68bc5e1271233ed2e41) e [Ultimate ASI Loader `71ca8ae020b0be3ff6daec4739e105b51410070f`](https://github.com/ThirteenAG/Ultimate-ASI-Loader/tree/71ca8ae020b0be3ff6daec4739e105b51410070f). Nenhuma dependência externa foi incorporada ou vinculada.

| Fonte primária | Fato usado | Licença / uso neste projeto |
| --- | --- | --- |
| [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk), [GameVersion.h](https://github.com/DK22Pac/plugin-sdk/blob/master/shared/GameVersion.h) | Distingue `GAME_10US_COMPACT` e `GAME_10US_HOODLUM`; não autoriza tratar endereços das duas variantes como iguais. | [Licença zlib](https://raw.githubusercontent.com/DK22Pac/plugin-sdk/master/LICENSE). Apenas referência; nenhum código copiado. |
| [gta-reversed/gta-reversed](https://github.com/gta-reversed/gta-reversed/blob/master/README.md) | Exige Compact de 5.189.632 bytes e avisa que não é o executável 1.0 US comum. Esse tamanho isolado não é uma identidade criptográfica. | Licença não confirmada nesta consulta; apenas referência, sem copiar código. |
| [SAEncyclopedia C0.1](https://github.com/TsyVM/SAEncyclopedia/blob/b873c48bdb372e87030ba708ac68fdabe13b9716/C0-Binary-Identity/01-the-hoodlum-layer.md) | Documenta Hoodlum de 14.383.616 bytes, PE i386, MD5 `170b3a9108687b26da2d8901c6948a18` e SHA-1 `185b73fbceaa05d66452691fc0d15c8d61b92a7e`; alerta para corpos de função relocados. | Referência de pesquisa; nenhum código copiado. Licença não confirmada. |
| [TsyVM/sa-sdk](https://github.com/TsyVM/sa-sdk/blob/99787a3c92019de04d61d68bc5e1271233ed2e41/README.md) | Declara suporte ao mesmo MD5 Hoodlum, C++20/MSVC x86 e estruturas com `static_assert`; as alegações de API não foram auditadas contra o binário local. | [MIT](https://github.com/TsyVM/sa-sdk/blob/99787a3c92019de04d61d68bc5e1271233ed2e41/LICENSE); avaliado, não integrado. |
| [ThirteenAG/Ultimate-ASI-Loader v7.7.0](https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v7.7.0/source/dllmain.cpp) | Carrega `.asi` com `LoadLibrary` e chama a exportação `InitializeASI`. Com `DontLoadFromDllMain=1` (padrão), adia a chamada até um wrapper de API; esse mecanismo não comprova, por si só, ausência de loader lock. Aceita `.asi` na raiz, `scripts` ou `plugins`. | [MIT](https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v7.7.0/license); loader não distribuído aqui. |
| [Microsoft: formato PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) | Assinaturas MZ/PE, máquina I386 e magic PE32 (`0x10b`) com limites do cabeçalho verificados. | Documentação da plataforma. |
| [Microsoft: DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices) | `DllMain` roda sob loader lock. Trabalho complexo e espera por threads ficam fora dele. | Documentação da plataforma. |
| [Microsoft: GetModuleFileNameW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamew), [BCryptHash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash) | Localização do executável do processo e SHA-256 do arquivo completo. `BCryptHash` requer Windows 10 ou posterior. | APIs do sistema. |

## Decisão de compatibilidade

O detector classifica como **GTA SA 1.0 US Hoodlum** somente a conjunção de cabeçalho PE32/i386, tamanho **14.383.616 bytes** e SHA-256 completo `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`. O SHA-256 foi fornecido pelo usuário para esta revisão; SAEncyclopedia e sa-sdk documentam o MD5 correspondente, e a SAEncyclopedia também documenta tamanho e SHA-1. [Um índice público de hashes](https://gridinsoft.com/online-virus-scanner/id/a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26) reúne o mesmo SHA-256, MD5, SHA-1 e tamanho; isso corrobora a correlação dos digests, sem comprovar origem legítima ou segurança do arquivo. Em Alpha 0.2.2, o arquivo da instalação isolada teve o SHA-256 conferido localmente. Tamanho, nome ou PE32 isolados nunca liberam reconhecimento. Compact, outros Hoodlum modificados, 1.0 EU, 1.01, Steam e remasterizações permanecem desconhecidos. A enumeração `GameVersion` permite acrescentar variantes individualmente no futuro.

| Marco | Estado nesta revisão | Evidência que falta |
| --- | --- | --- |
| Identidade binária Hoodlum de referência | Fingerprint registrado, classificador testado e SHA-256 local conferido | Leituras internas do jogo ainda não validadas |
| Carregamento do ASI | Confirmado pelo primeiro smoke informado pelo usuário com Ultimate ASI Loader Win32 7.7.0 | Sem pendência para o diagnóstico de entrada |
| Compatibilidade de APIs internas | ABI de `FindPlayerPed` e `FindPlayerCoors` e CALL de `gameProcessEvent` investigados no Hoodlum local | Duração dos objetos durante transições e ciclo de vida seguro do hook; ver [investigação específica](hoodlum-player-position.md) |
| Smoke físico de posição | Não executado | Spawn, posição, movimento, veículo e saída observados na instalação isolada |

Reconhecer o arquivo **não habilita hooks, leitura de ponteiros ou posição do jogador**. O módulo registra a identidade e informa que as APIs internas seguem desativadas.

O `GrandMP.asi` não depende de plugin-sdk nem de gta-reversed. O código de detecção testa limites do arquivo antes de cada leitura de cabeçalho. Lê no máximo 64 MiB do arquivo em disco, calcula SHA-256 do conteúdo completo lido e registra tamanho, timestamp e hash em `GrandMP.log`, no mesmo diretório do ASI. Falha de leitura, PE inválido ou falha de hash deixam o jogo sem integração.

O suporte inicial exige **Ultimate ASI Loader x86** com `[GlobalSets] DontLoadFromDllMain=1`, seu padrão documentado. `DllMain` apenas guarda o handle do módulo; não calcula hash, cria thread, fixa a DLL, carrega bibliotecas ou faz trabalho no término. O loader chama a exportação `InitializeASI` sincronicamente após `LoadLibrary`; nessa configuração, a carga é adiada até um wrapper de API, mas ainda não há prova de que a chamada concreta ocorra fora do loader lock. A exportação tem guarda atômica para processar no máximo uma chamada por carregamento, executa o diagnóstico e fecha o log antes de retornar. Nenhuma thread própria ou callback de término permanece ativo. Outros loaders que apenas chamam `LoadLibrary`, ou Ultimate ASI Loader com `DontLoadFromDllMain=0`, **não são suportados por este bootstrap**. O primeiro smoke de carregamento com Ultimate ASI Loader Win32 7.7.0 foi informado pelo usuário; o smoke de posição segue pendente.

## Dados necessários para a próxima validação

Em uma cópia isolada de instalação legítima do GTA SA clássico, sem compartilhar o executável ou assets:

```powershell
Get-Item -LiteralPath 'C:\caminho\isolado\gta_sa.exe' | Select-Object Name,Length
Get-FileHash -LiteralPath 'C:\caminho\isolado\gta_sa.exe' -Algorithm SHA256
```

Registrar também a origem e a versão do jogo, a versão e SHA-256 do loader ASI x86 selecionado, o Windows usado, e quaisquer patches que alterem o executável. Conferir se o hash do log coincide com `Get-FileHash` e com o SHA-256 Hoodlum acima. Uma identidade de arquivo não valida, por si só, a ABI de classes ou endereços de jogo: cada API e offset exige revisão específica.

## Primeiro smoke de carregamento informado; posição pendente

O usuário informou que o GTA SA 1.0 US Hoodlum iniciou em `D:\Jogos\GrandMP-Smoke` com Ultimate ASI Loader Win32 7.7.0, que `GrandMP.asi` carregou, que `InitializeASI` executou e que `GrandMP.log` confirmou a identidade Hoodlum. Essa evidência cobre carregamento e diagnóstico; não valida APIs internas nem posição. A investigação específica está em [hoodlum-player-position.md](hoodlum-player-position.md).

Preparar uma cópia isolada da instalação legítima e confirmar **Ultimate ASI Loader x86**, obtido e instalado manualmente. Confirmar `[GlobalSets] DontLoadFromDllMain=1`; `[GlobalSets] DebugLog=1` pode registrar o carregamento no log do loader. Copiar `GrandMP.asi` para pasta aceita por ele; não substituir o executável do jogo. Iniciar o GTA e verificar que inicia normalmente, que o loader invoca `InitializeASI` e que `GrandMP.log` contém versão GrandMP, plataforma, PE e hash. Comparar o hash com o arquivo instalado. Sair do jogo e observar se ocorre crash.

**Interromper aqui** se o arquivo instalado não corresponder exatamente ao hash acima ou se o loader x86 não estiver confirmado. Um arquivo correspondente deve gerar `Identidade binaria confirmada: GTA SA 1.0 US Hoodlum`; outro arquivo deve continuar desconhecido. O teste de existência do jogador e de coordenadas após spawn ainda não pode ser executado: falta comprovar pré-condições e duração dos objetos durante transições e implementar um mecanismo seguro de callback no ciclo do jogo. A ABI de chamada e retorno investigada está em [hoodlum-player-position.md](hoodlum-player-position.md). Não interpretar o log de identidade como smoke de leitura do jogador aprovado.

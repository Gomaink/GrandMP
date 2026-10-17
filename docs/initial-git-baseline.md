# Revisão da baseline Git inicial

Data da auditoria: 2026-10-13. O repositório ainda não possui commits. Esta revisão prepara a baseline até a Alpha 0.3.5 e o planejamento anterior à 0.3.6; não autoriza staging, commit ou publicação.

## Seleção versionável

O arquivo [baseline-files.txt](baseline-files.txt) é a lista fechada de paths relativos aprovada nesta revisão. Ele inclui:

- governança e configuração: `AGENTS.md`, `.codex/`, `agent-definitions/`, `.gitignore` e `.gitattributes`;
- build portátil: `CMakeLists.txt` da raiz e dos módulos;
- fontes próprias: `shared/`, `server/`, `client/windows/`, cliente sintético e placeholder Android;
- testes C++/Python/PowerShell e seus fontes descartáveis, sem artefatos compilados;
- scripts de diagnóstico e geração do registro;
- documentação técnica, roadmap, lockfile, catálogo gerado e perfil de compatibilidade.

As cópias em `.codex/agents/` e `agent-definitions/` são intencionais e coincidem byte a byte nesta auditoria. A primeira é a configuração ativa do workspace; a segunda preserva a distribuição documentada dos perfis.

Quando houver autorização expressa, o staging exato pode ser feito a partir da raiz com:

```powershell
git add --pathspec-from-file=docs/baseline-files.txt
```

Não usar `git add .`, `git add -A` ou diretórios como pathspec. Antes disso, comparar novamente os candidatos com o manifesto e revisar `git diff --cached --stat` e `git diff --cached`.

Preflight read-only para exigir igualdade exata e nenhuma entrada ignorada:

```powershell
$expected = @(Get-Content -LiteralPath 'docs/baseline-files.txt' | Sort-Object)
$actual = @(git ls-files --others --exclude-standard | Sort-Object)
$difference = @(Compare-Object $expected $actual)
if ($difference.Count -ne 0) { $difference; throw 'O working tree divergiu do manifesto.' }

$ignored = @($expected | git check-ignore --stdin --no-index)
if ($ignored.Count -ne 0) { $ignored; throw 'O manifesto contém paths ignorados.' }
```

## Exclusões verificadas

`build/` contém builds, executáveis, PDBs, logs, dumps/evidências físicas, caches de fontes, checkouts de pesquisa e binários de terceiros. `scripts/build/` contém probes compilados. Ambos permanecem ignorados. Também ficam ignorados caches de IDE/Python/CMake, objetos e bibliotecas, executáveis/DLL/ASI, símbolos, logs/dumps/temporários, arquivos de credenciais/chaves, archives e extensões comuns de assets GTA.

Nenhum candidato da lista contém byte NUL ou extensão de executável, biblioteca, símbolo, dump, archive, DFF/TXD/IMG/COL/IFP/SCM. A busca por padrões de segredo encontrou apenas nomes de parâmetros públicos (`password`) do catálogo. Paths locais da instalação isolada aparecem somente em documentação, scripts operacionais e fixture textual do CDB; não carregam credenciais.

O log fixture `scripts/tests/fixtures/loader-lock-20261013-002726.txt` é evidência textual necessária aos testes do parser. Ele não contém bytes do executável GTA; contém nomes de módulos, endereços, PID/TID históricos e o path conhecido da instalação de smoke.

## Terceiros e proveniência

Não há fonte vendorizado de plugin-sdk, MinHook, Detours, Ultimate ASI Loader, SA-MP/open.mp ou GTA. `tests/minhook-isolated/CMakeLists.txt` busca MinHook v1.3.4 por commit fixado durante um build opcional; o checkout e binários ficam em `build/`. Documentos apontam versões/licenças e deixam claro quando uma licença não foi confirmada.

O registro de compatibilidade guarda metadados derivados de interfaces públicas, URLs, assinaturas e resumos curtos, sem corpos de implementação ou includes completos. [THIRD_PARTY_NOTICES.md](compatibility/THIRD_PARTY_NOTICES.md) registra que os headers clássicos vêm de mirror comunitário sem licença de redistribuição identificada. Isso impede futura incorporação/distribuição dos headers sem decisão própria, mas não foi encontrado código desses headers nos módulos GrandMP desta baseline.

Há ainda um risco específico no artefato gerado: `catalog.json` contém resumos e trechos de retorno derivados de `openmultiplayer/web` e da wiki Streamer, cujos snapshots não apresentaram licença raiz identificada, além de reproduzir sistematicamente assinaturas/constantes dos mirrors clássicos. O uso como pesquisa local está registrado e atribuído, mas a auditoria não conclui permissão de redistribuição desse corpus derivado. Portanto, **a seleção é GO para commit local e NO-GO para push/publicação** enquanto não houver confirmação de licença/permissão ou regeneração do catálogo apenas com metadados próprios/factuais e links. `catalog.json` e `catalog.md` permanecem na seleção para preservar o estado aprovado local; removê-los ou transformá-los exige decisão explícita antes de publicação.

O projeto GrandMP ainda não possui uma licença própria na raiz. Isso não impede uma baseline privada, porém mantém o código sob direitos reservados por padrão e é bloqueio para uma publicação com termos claros. A escolha de licença exige decisão do proprietário; não foi presumida nesta auditoria.

## Reprodução do catálogo

Foi executada uma reprodução a partir de `build/baseline-registry-repro/` vazio:

```powershell
python scripts/compatibility/generate_registry.py --fetch --sources build/baseline-registry-repro
python scripts/compatibility/generate_registry.py --fetch --sources build/baseline-registry-repro --check
python -m unittest discover -s scripts/compatibility -p 'test_*.py' -v
```

O primeiro teste expôs hashes da wiki Streamer dependentes de CRLF do checkout local. `sources.lock.json` foi corrigido para os bytes dos blobs obtidos por `git show` no commit fixado. Após a correção: geração por cache vazio PASS, `--check` PASS e **19/19 testes PASS**. As demais fontes são baixadas por commit completo; todo arquivo é verificado por path, tamanho e SHA-256. O catálogo versionado não depende do cache ignorado, embora uma reprodução sem rede exija cache previamente preenchido.

A árvore C++ foi configurada e compilada novamente com Visual Studio 2026/MSVC 19.51.36260 em diretórios ignorados:

- Win32 Debug: build PASS; CTest **106/106 PASS**.
- x64 Debug: build PASS; CTest **97/97 PASS**.

Esses testes não executaram GTA e não convertem laboratórios nativos em integração homologada.

## Riscos antes do primeiro commit

- Decidir se a baseline será privada ou escolher uma licença própria antes de publicação/distribuição.
- Resolver a redistribuição do catálogo derivado antes de qualquer push/publicação: confirmar permissões ou gerar uma forma sem resumos/trechos de terceiros e avaliar separadamente o inventário integral de assinaturas clássicas.
- O catálogo possui 4.614 ocorrências PENDING e 134 ocorrências chamáveis sem semântica fechada; é planejamento, não implementação nem promessa de compatibilidade.
- O manifesto deve ser revalidado se qualquer arquivo for criado ou alterado antes do staging.
- Não versionar evidências brutas sob `build/runtime-evidence/`; fixtures mínimas e sanitizadas permanecem em `scripts/tests/fixtures/`.
- A política ampla para archives, chaves e assets GTA exige revisão explícita/`git add -f` se um futuro fixture legítimo precisar dessas extensões.

## Mensagem recomendada

```text
chore(repo): establish GrandMP baseline through alpha 0.3.5
```

Corpo sugerido:

```text
- add portable core, Windows diagnostics, observer and UDP replication foundation
- add native lifecycle laboratories and regression tests
- document verified limits, product roadmap and compatibility registry
```

Nenhum commit foi criado por esta revisão.

Decisão desta auditoria: **GO para staging/commit local somente após autorização expressa; NO-GO para push ou publicação no estado atual.**

# Validação do planejamento e inventário — 2026-10-13

Escopo: roadmap, registro inicial de API e ferramentas de extração. Nenhuma funcionalidade de rede, engine, Pawn ou ASI foi implementada. Nenhum GTA foi executado. Builds/CTests C++ anteriores são históricos, não reexecutados neste trabalho documental.

## Resultados executados

Ambiente: Windows, Python 3.14.6, biblioteca padrão; fontes em cache ignorado. Na raiz do repositório:

```powershell
python scripts/compatibility/generate_registry.py
if ($LASTEXITCODE -ne 0) { throw 'Falha na geração' }
python -m unittest discover -s scripts/compatibility -p 'test_*.py' -v
if ($LASTEXITCODE -ne 0) { throw 'Falha nos testes' }
python scripts/compatibility/generate_registry.py --check
if ($LASTEXITCODE -ne 0) { throw 'Inventário não reproduzível' }
```

- **19/19 testes PASS**, executados pelo agente principal e novamente pelo reviewer na versão final do gerador.
- **Reprodução determinística PASS**: JSON/Markdown gerados coincidem com os arquivos entregues; hashes e tamanhos de todos os inputs fixados são verificados.
- Cobertura: comentários/strings, assinaturas multilinha/tags/referências/defaults/varargs, qualificadores static, stocks sem corpo copiado, aliases, macros/hex, branches, enums implícitos, forward versus callback, depreciações sem propagação indevida, variável pública, operadores, erro de parsing, fonte adulterada e paths inválidos. Catálogo: IDs únicos, campos obrigatórios/status, contextos NPC e servidor, mudança de kind entre clássico/open.mp, stocks obsoletos, nomes reais do perfil essencial.
- Cache de pesquisa e bytecode Python estão ignorados; nenhum código ou binário de dependência foi incorporado ao produto. Nenhum staging/commit foi executado.

## Inventário observado

| Fonte | Ocorrências de declarações | Ocorrências públicas chamáveis com semântica ainda insuficiente |
|---|---:|---:|
| SA-MP R2 | 856 | 8 |
| SA-MP DL | 868 | 8 |
| open.mp | 2.552 | 9 |
| Streamer | 338 | 109 |
| **Total** | **4.614** | **134** |

Todos os registros são **PENDING**. Totais incluem nomes repetidos, contextos/versões/branches diferentes, constantes, macros e infraestrutura; não são quantidade de funções distintas nem suporte funcional. A seção de lacunas no [catálogo Markdown](catalog.md) permite revisar cada ocorrência insuficiente. Outros resumos permanecem UNREVIEWED até fechamento do contrato individual.

O perfil proposto de Alpha 0.8 possui **63 requisitos nominais**: 42 natives, 14 callbacks e 7 macros constantes, todos encontrados no snapshot R2/contexto servidor. É um subconjunto de produto planejado, sem VM/bindings/compatibilidade hoje.

## Revisões e correções

**architect — GO documental:** estado real, matriz, paralelismo e contador → ponte observer/ASI → ped refletem os módulos existentes. Esclarecimento aplicado: remover referência externa para testar residência é operação de host descartável; GTA mantém política process-lifetime, sem FreeLibrary da referência do loader.

**reviewer — aprovado após correções, read-only:** corrigida ancestralidade de APIs clássicas convertidas em stock/forward/macro no open.mp; pragmas de depreciação preservados e divergência Get*PoolSize documentada. Regressões executadas na suíte final. Não há bloqueio identificado para entregar o planejamento/inventário inicial.

## Limites e decisão

- Scanner é lexical, não compiler/pré-processador Pawn. Não prova quantas funções estão habilitadas em uma configuração nem cobertura AMX.
- Semântica e dependências são metadados iniciais; 134 lacunas e variantes depreciadas exigem revisão/fonte suficiente antes de implementação. Não há stub promovido a implementado.
- Classic SA-MP provém de mirror público, não autenticado contra pacote original; licenças/proveniência para futura incorporação de includes/VM/plugins permanecem gates. Cobertura de plugin é apenas Streamer; não totaliza o ecossistema SA-MP.
- Catálogo preserva declarações e resumos referenciados, não corpos de implementação nem executáveis de terceiros.
- Observação LoaderLock FREE_SNAPSHOT não autoriza patch, callback ou engine; integração real continua **NO-GO** até gates específicos de [ROADMAP.md](../ROADMAP.md).

**GO para o roadmap e o registro inicial.** Os próximos três slices são recomendações finitas, não execução autorizada por este relatório. Nenhum hook, ped, protocolo novo, release ou commit foi iniciado.

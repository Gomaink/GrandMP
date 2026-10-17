"""Declaration inventory, NOT a Pawn compiler/preprocessor or runtime binding.

Only stdlib. Exact source bytes are checked against sources.lock.json. All
conditional declarations are retained separately; no implicit enum value is
invented. Third-party implementations are never copied to the registry.
"""
from __future__ import annotations

import argparse
import bisect
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import html
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import urllib.request
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
CALLABLES = {"native", "callback", "forward", "stock", "public"}
PAWN_FILES = {"args.inc", "console.inc", "core.inc", "datagram.inc", "file.inc", "float.inc", "string.inc", "time.inc"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def uncomment(text: str) -> tuple[str, list[tuple[int, int, str]]]:
    out = list(text)
    comments = []
    i = 0
    while i < len(text):
        if text[i] in "\"'":
            quote = text[i]
            i += 1
            while i < len(text):
                if text[i] in "\\^":
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
        elif text.startswith("//", i) or text.startswith("/*", i):
            start = i
            if text.startswith("//", i):
                end = text.find("\n", i)
                end = len(text) if end < 0 else end
            else:
                end = text.find("*/", i + 2)
                if end < 0:
                    raise ValueError("Unterminated comment")
                end += 2
            comments.append((start, end, text[start:end]))
            for j in range(start, end):
                if text[j] not in "\r\n":
                    out[j] = " "
            i = end
        else:
            i += 1
    return "".join(out), comments


TOKEN = re.compile(r'"(?:[\\^].|[^"\\^])*"|\'(?:[\\^].|[^\'\\^])*\'|[A-Za-z_@][A-Za-z0-9_@]*|0[xX][0-9a-fA-F_]+|0[bB][01_]+|[0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?|\.\.\.|==|!=|<=|>=|<<|>>|\S', re.S)
IDENT = re.compile(r"^[A-Za-z_@][A-Za-z0-9_@]*$")


def parse_include(text: str) -> tuple[list[dict], list[str]]:
    clean, comments = uncomment(text)
    starts = [0] + [m.end() for m in re.finditer("\n", text)]
    lines = clean.splitlines(keepends=True)
    conditions = {}
    stack: list[str] = []
    items: list[dict] = []
    imports: list[str] = []
    masked = list(clean)

    def emit(kind: str, name: str, begin: int, end: int, **extra):
        line = bisect.bisect_right(starts, begin)
        prior = ""
        for c in reversed(comments):
            if c[1] > begin or "<summary>" not in c[2]:
                continue
            between = re.sub(r"(?m)^\s*#.*$", "", clean[c[1]:begin])
            if not re.sub(r"\bstatic\b", "", between).strip():
                prior = c[2]
            break
        signature = clean[begin:end].strip() if kind in {"macro", "constant_macro"} else " ".join(m.group() for m in TOKEN.finditer(clean[begin:end])).strip()
        deprecations = []
        for preceding in reversed(clean[:begin].splitlines()):
            stripped = preceding.strip()
            deprecated = re.match(r"#pragma\s+deprecated\b\s*(.*)", stripped)
            if deprecated:
                deprecations.append(deprecated[1])
            elif stripped and not stripped.startswith("#"):
                break
        items.append({"kind": kind, "name": name, "line": line,
                      "column": begin - starts[line - 1] + 1,
                      "signature": signature,
                      "conditions": list(conditions.get(line, [])),
                      "comment": prior, "deprecation_notices": list(reversed(deprecations)), **extra})

    index = 0
    while index < len(lines):
        line = lines[index]
        number = index + 1
        conditions[number] = list(stack)
        match = re.match(r"\s*#(\w+)\b(.*)", line)
        if not match:
            index += 1
            continue
        begin = starts[index] + len(line) - len(line.lstrip())
        logical = line.rstrip("\r\n")
        last = index
        while logical.rstrip().endswith("\\"):
            logical = logical.rstrip()[:-1] + " "
            last += 1
            if last >= len(lines):
                raise ValueError(f"Unterminated directive at {number}")
            conditions[last + 1] = list(stack)
            logical += lines[last].strip()
        directive, argument = re.match(r"\s*#(\w+)\b(.*)", logical).groups()
        argument = argument.strip()
        if directive == "define":
            macro = re.match(r"([A-Za-z_@][A-Za-z0-9_@]*)(.*)", argument)
            if not macro:
                raise ValueError(f"Unsupported macro at {number}: {argument}")
            name, tail = macro.groups()
            function_like = tail.startswith("(") or tail.startswith(":")
            kind = "macro" if function_like or not tail.strip() or name.startswith("_") or name.upper() != name else "constant_macro"
            emit(kind, name, begin, starts[last] + len(lines[last]), value_expression=tail.strip(), pattern_macro=function_like)
        elif directive in {"include", "tryinclude"}:
            imports.append(argument)
        elif directive in {"if", "ifdef", "ifndef"}:
            stack.append(f"#{directive} {argument} (line {number})")
        elif directive in {"else", "elseif", "elif"}:
            if not stack:
                raise ValueError(f"Unmatched #{directive} at {number}")
            stack[-1] = f"#{directive} {argument}; branch of {stack[-1]} (line {number})"
        elif directive == "endif":
            if not stack:
                raise ValueError(f"Unmatched #endif at {number}")
            stack.pop()
        for j in range(starts[index], starts[last] + len(lines[last])):
            if masked[j] not in "\r\n":
                masked[j] = " "
        index = last + 1
    if stack:
        raise ValueError("Unclosed preprocessor conditional")

    tokens = list(TOKEN.finditer("".join(masked)))

    def split_parts(begin: int, end: int):
        part = begin
        level = 0
        for j in range(begin, end):
            v = tokens[j].group()
            if v in ("(", "[", "{"):
                level += 1
            elif v in (")", "]", "}"):
                level -= 1
            elif v == "," and level == 0:
                yield part, j
                part = j + 1
        if part < end:
            yield part, end

    def member_name(begin: int, end: int):
        stop = next((j for j in range(begin, end) if tokens[j].group() in {"=", "[", "{"}), end)
        names = [tokens[j].group() for j in range(begin, stop) if IDENT.match(tokens[j].group())]
        return names[-1] if names else None

    i = 0
    while i < len(tokens):
        word = tokens[i].group()
        if word not in {"native", "forward", "stock", "public", "const", "enum"}:
            if word == "{":
                depth = 1
                i += 1
                while i < len(tokens) and depth:
                    depth += (tokens[i].group() == "{") - (tokens[i].group() == "}")
                    i += 1
            else:
                i += 1
            continue
        begin = i - 1 if i and tokens[i - 1].group() == "static" else i
        if word == "enum":
            brace = next((j for j in range(i + 1, len(tokens)) if tokens[j].group() in {"{", ";"}), None)
            if brace is None or tokens[brace].group() != "{":
                raise ValueError(f"Unparsed enum at {tokens[i].start()}")
            end, depth = brace + 1, 1
            while end < len(tokens) and depth:
                depth += (tokens[end].group() == "{") - (tokens[end].group() == "}")
                end += 1
            if depth:
                raise ValueError("Unclosed enum")
            header = " ".join(t.group() for t in tokens[i + 1:brace]) or "(anonymous)"
            emit("enum", header, tokens[i].start(), tokens[brace].start())
            for a, b in split_parts(brace + 1, end - 1):
                name = member_name(a, b)
                if not name:
                    raise ValueError("Unparsed enum member")
                eq = next((j for j in range(a, b) if tokens[j].group() == "="), None)
                value = None if eq is None else " ".join(t.group() for t in tokens[eq + 1:b])
                emit("enum_member", name, tokens[a].start(), tokens[b - 1].end(), value_expression=value, enum_context=header)
            i = end
            continue
        j = i + 1
        parens = 0
        brackets = 0
        braces = 0
        function_open = None
        while j < len(tokens):
            v = tokens[j].group()
            if v == "(" and parens == 0:
                # A tag macro has ':' following its matching close; it is
                # not the function argument list.
                k, depth = j + 1, 1
                while k < len(tokens) and depth:
                    depth += (tokens[k].group() == "(") - (tokens[k].group() == ")")
                    k += 1
                if not depth and (k >= len(tokens) or tokens[k].group() != ":") and word != "const":
                    function_open = j
                parens += 1
            elif v == "(":
                parens += 1
            elif v == ")":
                parens -= 1
            elif v == "[":
                brackets += 1
            elif v == "]":
                brackets -= 1
            elif v == "{" and parens == 0 and brackets == 0 and braces == 0 and function_open is not None:
                break
            elif v == "{":
                braces += 1
            elif v == "}":
                braces -= 1
            elif v == ";" and parens == brackets == braces == 0:
                break
            j += 1
        if j >= len(tokens):
            raise ValueError(f"Unclosed {word} declaration")
        if word == "const" or function_open is None:
            for a, b in split_parts(i + 1, j):
                name = member_name(a, b)
                if not name:
                    raise ValueError(f"Unparsed {word} variable")
                eq = next((k for k in range(a, b) if tokens[k].group() == "="), None)
                value = None if eq is None else " ".join(t.group() for t in tokens[eq + 1:b])
                emit("constant" if word == "const" else "public_variable" if word == "public" else "stock_variable", name, tokens[begin].start(), tokens[j].end(), value_expression=value)
        else:
            prefix = [t.group() for t in tokens[i + 1:function_open]]
            if "operator" in prefix:
                name = "".join(prefix[prefix.index("operator"):])
            else:
                name = prefix[-1]
                if not IDENT.match(name):
                    raise ValueError(f"Unparsed callable name: {prefix}")
            kind = "callback" if word == "forward" and (name.startswith("On") or name.startswith("Streamer_On")) else word
            eq = next((k for k in range(function_open, j) if tokens[k].group() == "=" and k > function_open and tokens[k - 1].group() == ")"), None)
            binding = None if eq is None else "".join(t.group() for t in tokens[eq + 1:j])
            emit(kind, name, tokens[begin].start(), tokens[j].start() if tokens[j].group() == "{" else tokens[j].end(), binding=binding)
        if tokens[j].group() == "{":
            depth = 1
            j += 1
            while j < len(tokens) and depth:
                depth += (tokens[j].group() == "{") - (tokens[j].group() == "}")
                j += 1
            if depth:
                raise ValueError("Unclosed function body")
            i = j
        else:
            i = j + 1
    return items, sorted(set(imports))


def plain(value: str, limit=65) -> str:
    value = html.unescape(re.sub(r"<[^>]+>", " ", value))
    value = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", value)
    value = re.sub(r"[`*_]", "", value)
    words = value.split()
    return " ".join(words[:limit]) + (" …" if len(words) > limit else "")


def documentation(files: dict[str, bytes]):
    docs = {}
    for path, data in files.items():
        match = re.fullmatch(r"frontend/docs/scripting/(functions|callbacks)/([^/]+)\.md", path)
        if not match:
            continue
        text = data.decode("utf-8-sig")
        summary = re.search(r"(?m)^description:\s*(.+)$", text)
        ret = re.search(r"(?ms)^## Returns\s*\n(.*?)(?=^## |\Z)", text)
        docs[(match[1], match[2])] = {"path": path, "summary": plain(summary[1].strip().strip('\"')) if summary else None,
                                     "returns_excerpt": plain(ret[1]) if ret else None}
    return docs


def plugin_documentation(files: dict[str, bytes], core_docs: dict):
    result = {}
    for path, data in sorted(files.items()):
        text = data.decode("utf-8-sig")
        anchors = list(re.finditer(r'(?m)^#### <a name="([A-Za-z0-9_]+)">.*$', text))
        for index, match in enumerate(anchors):
            body = text[match.end():anchors[index + 1].start() if index + 1 < len(anchors) else len(text)]
            prose = next((line for line in body.splitlines() if line.strip() and not line.startswith((">", "*", "#", "["))), None)
            result[match[1]] = {"path": path, "summary": plain(prose) if prose else None, "basis": "plugin_wiki"}
        for match in re.finditer(r'(?m)^\* \*\*([A-Za-z0-9_]+)\*\*', text):
            name = match[1]
            core_name = name.replace("Dynamic", "").replace("DynObject", "Object")
            core = core_docs.get(("callbacks" if name.startswith("On") else "functions", core_name))
            summary = ("Variante dinâmica/streaming do plugin; operação base: " + core["summary"] + " Revisar diferenças de IDs, filtros e emissão na wiki.") if core and core["summary"] else None
            result.setdefault(name, {"path": path, "summary": summary, "basis": "plugin_wiki_with_core_analogy" if summary else "plugin_wiki_declaration_only"})
    return result


def category(name: str, file: str, doc: dict | None):
    for pattern, group in [
        (r"TextDraw", "TextDraws"), (r"TextLabel|3DText", "TextLabels"), (r"Dialog", "Dialogs"),
        (r"GangZone", "GangZones"), (r"Checkpoint|RaceCP|DynamicCP", "Checkpoints"),
        (r"Pickup", "Pickups"), (r"Actor", "Actors"), (r"Object|Material", "Objects"),
        (r"NPC|RecordingPlayback", "NPC"), (r"Vehicle|Trailer", "Vehicles"),
        (r"ClientMessage|PlayerText|ChatBubble", "Chat"), (r"Animation", "Animations"),
        (r"Camera", "Camera"), (r"Audio|Sound", "Audio"), (r"Menu", "Menus"),
        (r"Timer", "Timers"), (r"MapIcon", "MapIcons"),
        (r"VirtualWorld|Interior|Weather|Gravity|WorldTime|WorldBounds|Explosion|ZoneName", "World"),
        (r"Player|KEY_|SPECIAL_ACTION|WEAPON_", "Players")]:
        if re.search(pattern.replace("_", ""), name.replace("_", ""), re.I):
            return group
    suffix = Path(file).stem.removeprefix("omp_").removeprefix("a_")
    return {"player": "Players", "players": "Players", "vehicle": "Vehicles", "vehicles": "Vehicles",
            "actor": "Actors", "object": "Objects", "objects": "Objects", "npc": "NPC", "http": "HTTP",
            "sampdb": "Database", "database": "Database", "checkpoint": "Checkpoints", "class": "Classes",
            "core": "Server" if file.startswith("omp_") else "PawnRuntime", "samp": "Server",
            "network": "Network", "variable": "Variables", "dialog": "Dialogs", "gangzone": "GangZones",
            "pickup": "Pickups", "textdraw": "TextDraws", "textlabel": "TextLabels", "menu": "Menus",
            "float": "Math", "string": "Strings", "file": "Files", "time": "Time", "console": "Console",
            "datagram": "Network", "args": "Console", "streamer": "Streaming"}.get(suffix, "Toolchain")


def plan_dependencies(group: str, kind: str, context: str):
    if kind in {"macro", "constant_macro", "constant", "enum", "enum_member"}:
        return ["pawn.compiler_profile", "include.constants_and_tags", f"contract.{group.lower()}"]
    if kind == "stock":
        return ["pawn.compiler_profile", "stock.transitive_bindings_review", f"contract.{group.lower()}"]
    base = ["pawn.amx_abi", "pawn.scheduler_and_bindings", f"contract.{group.lower()}"]
    if context == "npc_script":
        base.append("npc.script_context")
    if group in {"Players", "Vehicles", "Actors", "Objects", "Pickups", "Checkpoints", "Classes", "World", "Animations", "Camera", "Audio", "NPC", "Streaming", "MapIcons"}:
        base += ["server.domain_authority", "client.native_execution", f"client.backend.{group.lower()}"]
    elif group in {"TextDraws", "TextLabels", "Dialogs", "GangZones", "Menus", "Chat"}:
        base += ["server.targeted_events", f"client.presentation.{group.lower()}"]
    return base


def test_criteria(kind: str, name: str):
    if kind in {"constant", "constant_macro", "macro", "enum", "enum_member"}:
        return ["Compilar usos positivos/negativos no perfil Pawn fixado, incluindo guards e variantes condicionais.",
                "Comparar expressão/expansão, tags, valores e limites com a fonte; não avaliar enum implícito por suposição."]
    if kind == "stock":
        return ["Compilar e executar chamadas com a assinatura exata; verificar dependências transitivas da implementação stock.",
                "Comparar retorno, referências, limites e erros; stub de native transitivo não satisfaz o teste."]
    if kind == "callback":
        return [f"Produzir o evento {name} por estado real; validar argumentos, ordem, reentrância, retorno e propagação/cancelamento.",
                "Executar teste servidor ou smoke GTA conforme domínio; chamada manual ao public não prova emissão do evento."]
    return ["Validar tags, aridade, referências/arrays/defaults/varargs e binding AMX; compilar casos aceitos e rejeitados.",
            "Comparar retorno/efeitos/casos de erro, IDs inválidos e limites com contrato revisado da fonte.",
            "Executar integração servidor ou smoke GTA aplicável; efeito observável é obrigatório, sem stubs."]


def load_inputs(lock: dict, cache: Path, fetch: bool):
    # A reviewed lock selects text files within the cache, never arbitrary paths.
    seen_sources = set()
    for source in lock["sources"]:
        sid = source["id"]
        if not re.fullmatch(r"[A-Za-z0-9_-]+", sid) or sid in seen_sources:
            raise ValueError("Invalid/duplicate source id")
        seen_sources.add(sid)
        if fetch and (not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", source.get("repository", "")) or
                      not re.fullmatch(r"[0-9a-f]{40}", source.get("commit", ""))):
            raise ValueError("Fetch requires a pinned repository and full commit")
        seen_paths = set()
        for file in source["files"]:
            value = file["path"]
            path = PurePosixPath(value)
            if (not value or "\\" in value or ":" in value or path.is_absolute() or
                    any(part in {"", ".", ".."} for part in value.split("/")) or value in seen_paths or
                    not re.fullmatch(r"[0-9a-f]{64}", file["sha256"])):
                raise ValueError("Invalid/duplicate pinned source path or hash")
            seen_paths.add(value)
    specs = [(source, file) for source in lock["sources"] for file in source["files"]]

    # Wiki transport has no commit-pinned raw HTTP endpoint. A bare reference
    # checkout is used only with --fetch; git show reads text, never runs it.
    wiki_repos = {}
    for source in lock["sources"]:
        if source.get("transport") != "git-wiki":
            continue
        missing = any(not (cache / source["id"] / f["path"]).is_file() or digest((cache / source["id"] / f["path"]).read_bytes()) != f["sha256"] for f in source["files"])
        if fetch and missing:
            gitdir = cache / ("." + source["id"] + "-git")
            if not gitdir.exists():
                subprocess.run(["git", "clone", "--bare", "https://github.com/" + source["repository"] + ".wiki.git", str(gitdir)], check=True)
            subprocess.run(["git", "--git-dir", str(gitdir), "fetch", "origin", source["commit"]], check=True)
            wiki_repos[source["id"]] = gitdir

    def read(spec):
        source, file = spec
        path = cache / source["id"] / file["path"]
        data = path.read_bytes() if path.is_file() else None
        if data is None or digest(data) != file["sha256"]:
            if not fetch:
                raise ValueError(f"Missing/changed pinned source: {path}")
            url = f'https://raw.githubusercontent.com/{source["repository"]}/{source["commit"]}/{file["path"]}'
            if source.get("transport") == "git-wiki":
                data = subprocess.run(["git", "--git-dir", str(wiki_repos[source["id"]]), "show", source["commit"] + ":" + file["path"]], check=True, capture_output=True).stdout
            else:
                with urllib.request.urlopen(url, timeout=45) as response:
                    data = response.read()
            if digest(data) != file["sha256"]:
                raise ValueError(f"Hash mismatch: {url}")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        if "bytes" in file and len(data) != file["bytes"]:
            raise ValueError(f"Size mismatch: {path}")
        return source["id"], file["path"], data

    result = {source["id"]: {} for source in lock["sources"]}
    with ThreadPoolExecutor(max_workers=8) as pool:
        for sid, path, data in pool.map(read, specs):
            result[sid][path] = data
    return result


def build_registry(lock: dict, inputs: dict):
    raw = []
    imports = {}
    sources = {s["id"]: s for s in lock["sources"]}
    docs_source = next(s for s in lock["sources"] if s["role"] == "documentation")
    docs = documentation(inputs[docs_source["id"]])
    wiki_source = next((s for s in lock["sources"] if s["role"] == "plugin_documentation"), None)
    wiki_docs = plugin_documentation(inputs[wiki_source["id"]], docs) if wiki_source else {}
    for source in lock["sources"]:
        if source["role"] != "includes":
            continue
        for path, data in sorted(inputs[source["id"]].items()):
            if not path.endswith(".inc"):
                continue
            declarations, imported = parse_include(data.decode("utf-8-sig"))
            imports[source["id"] + ":" + path] = imported
            for decl in declarations:
                raw.append({**decl, "source_id": source["id"], "source_file": path})
    # A published operation can become a stock/forward/macro in modern
    # includes. Ancestry by declared name/context is not ABI equivalence.
    classical = {("npc_script" if d["source_file"] == "a_npc.inc" else "server_script", d["name"])
                 for d in raw if sources[d["source_id"]]["family"] == "SAMP_CLASSIC"}
    entries = []
    for decl in raw:
        source = sources[decl["source_id"]]
        kind, name, path = decl["kind"], decl["name"], decl["source_file"]
        doc = docs.get(("callbacks" if kind == "callback" else "functions", name))
        if not doc and decl.get("binding"):
            doc = docs.get(("functions", decl["binding"]))
        internal = name.startswith(("_", "@")) or name.startswith("operator") or (kind == "macro" and decl.get("value_expression", "").startswith(":"))
        group = "Toolchain" if internal else category(name, path, doc)
        context = "npc_script" if path == "a_npc.inc" else "server_script"
        family = source["family"]
        if source["id"] == "omp":
            if path in PAWN_FILES:
                family = "PAWN_STDLIB"
            elif not internal and (context, name) in classical:
                family = "OPENMP_VARIANT_OF_SAMP"
            else:
                family = "OPENMP_EXTENSION" if not internal else "INCLUDE_INFRASTRUCTURE"
        # Legacy a_npc is a separate NPC program. Modern omp_npc contains
        # server-side NPC management; it is not the legacy NPC VM context.
        comment = decl.pop("comment")
        summary_match = re.search(r"<summary>(.*?)</summary>", comment, re.S)
        summary = doc.get("summary") if doc else None
        summary_source = "documentation" if summary else "include_comment" if summary_match else "declaration_only"
        if not summary and summary_match:
            summary = plain(summary_match[1])
        wiki_doc = wiki_docs.get(name) if source["family"] == "THIRD_PARTY_PLUGIN" else None
        if not summary and wiki_doc and wiki_doc["summary"]:
            summary, summary_source = wiki_doc["summary"], wiki_doc["basis"]
        if not summary:
            summary = f'Declaração {kind} de {name}; ' + ("a expressão literal/enum condiciona o compilador, sem chamada nativa." if kind not in CALLABLES else "contrato comportamental requer revisão individual da referência antes de implementar.")
        doc_url = f'https://github.com/{docs_source["repository"]}/blob/{docs_source["commit"]}/{doc["path"]}' if doc else None
        if wiki_doc:
            doc_url = f'https://github.com/{wiki_source["repository"]}/wiki/{quote(Path(wiki_doc["path"]).stem)}/{wiki_source["commit"]}'
        identity = f'{source["id"]}:{path}:{decl["line"]}:{decl["column"]}:{kind}:{name}'
        dependencies = plan_dependencies(group, kind, context)
        if context == "server_script" and name in {"GetPlayerPos", "GetPlayerName", "IsPlayerConnected", "GetPlayerPoolSize", "GetPlayerState", "GetPlayerHealth", "GetPlayerArmour", "GetPlayerScore", "GetPlayerMoney", "GetPlayerInterior", "GetPlayerVirtualWorld"}:
            dependencies = ["pawn.amx_abi", "pawn.scheduler_and_bindings", "server.player_identity", "server.player_snapshot", "reference_output_and_invalid_id_contract"]
        if context == "server_script" and name in {"OnGameModeInit", "OnGameModeExit", "OnPlayerConnect", "OnPlayerDisconnect", "OnIncomingConnection"}:
            dependencies = ["pawn.amx_abi", "pawn.callback_dispatch_order", "server.session_to_player_lifecycle"]
        entries.append({"id": identity, **decl, "category": group, "origin_family": family,
                        "origin_matching_basis": "SHARED_DECLARED_NAME_AND_CONTEXT_NOT_BEHAVIOR" if family == "OPENMP_VARIANT_OF_SAMP" else "SOURCE_FAMILY",
                        "source_version": source["version"], "source_commit": source["commit"],
                        "source_url": f'https://github.com/{source["repository"]}/blob/{source["commit"]}/{path}#L{decl["line"]}',
                        "execution_context": context, "visibility": "internal" if internal else "public",
                        "compiled_native_name": (decl.get("binding") or name) if kind == "native" and not internal else None,
                        "documentation_placeholder": name.startswith("__open_mp_unused_"),
                        "semantics": {"summary": summary, "basis": summary_source, "reference": doc_url,
                                      "returns_excerpt": doc.get("returns_excerpt") if doc and context != "npc_script" else None,
                                      "variant_caution": "Resumo/retorno documentais podem divergir desta implementação stock; revisar corpo e pragmas da fonte fixada." if kind == "stock" else None,
                                      "review_state": "UNREVIEWED", "caution": "Docs open.mp podem descrever correções/extensões; não transferir automaticamente para SA-MP clássico."},
                        "grandmp_status": "PENDING", "implementation_evidence": [],
                        "dependencies": dependencies, "dependencies_basis": "INITIAL_PLAN_REQUIRES_ITEM_REVIEW",
                        "test_criteria": test_criteria(kind, name)})
    entries.sort(key=lambda e: (e["source_id"], e["source_file"], e["line"], e["column"], e["kind"], e["name"]))
    ids = [e["id"] for e in entries]
    if len(ids) != len(set(ids)):
        raise ValueError("Duplicate declaration identity")
    summary = {}
    for sid in sources:
        rows = [e for e in entries if e["source_id"] == sid]
        summary[sid] = {"declaration_occurrences": len(rows), "by_kind": dict(sorted(Counter(e["kind"] for e in rows).items())),
                        "public_callable_names": len({(e["execution_context"], e["name"]) for e in rows if e["kind"] in CALLABLES and e["visibility"] == "public"}),
                        "semantic_declaration_only_public_callables": sum(e["visibility"] == "public" and e["kind"] in CALLABLES and e["semantics"]["basis"] == "declaration_only" for e in rows)}
    return {"schema_version": 1, "scope": "Source declaration inventory; conditions not evaluated; no runtime compatibility claim",
            "sources_lock_sha256": digest(json.dumps(lock, sort_keys=True, ensure_ascii=False).encode()),
            "summary": summary, "include_imports": imports, "entries": entries}


def markdown(registry: dict):
    lines = ["# Catálogo gerado de declarações Pawn", "", "Gerado pelo inventário fixado em `sources.lock.json`. JSON contém assinatura, origem, condição, semântica, dependências e testes por declaração.", "",
             "**Todos os itens estão PENDING.** Nomes abaixo não são natives implementadas no GrandMP. Contagens são declarações, não funções únicas nem chamadas efetivamente habilitadas pelo compilador.", "",
             "| Fonte | Declarações | Nomes públicos chamáveis por contexto | Declarações públicas chamáveis sem semântica fechada |", "|---|---:|---:|---:|"]
    for sid, stats in registry["summary"].items():
        if stats["declaration_occurrences"]:
            lines.append(f'| {sid} | {stats["declaration_occurrences"]} | {stats["public_callable_names"]} | {stats["semantic_declaration_only_public_callables"]} |')
    lines += ["", "## Categorias", "", "| Categoria | Declarações públicas |", "|---|---:|"]
    for cat, count in sorted(Counter(e["category"] for e in registry["entries"] if e["visibility"] == "public").items()):
        lines.append(f"| {cat} | {count} |")
    lines += ["", "## Lacunas de semântica para revisão", "", "Existência e assinatura verificadas, mas sem contrato comportamental suficiente na documentação consumida. Estes itens exigem referência/revisão individual antes de implementação; todos os demais resumos também permanecem UNREVIEWED.", "", "| Fonte | Contexto | Declaração | Referência |", "|---|---|---|---|"]
    for e in registry["entries"]:
        if e["visibility"] == "public" and e["kind"] in CALLABLES and e["semantics"]["basis"] == "declaration_only":
            lines.append(f'| {e["source_id"]} | {e["execution_context"]} | {e["name"]} | [L{e["line"]}]({e["source_url"]}) |')
    lines += ["", "## Funções e callbacks públicos", "", "Uma linha por declaração/variante, preservando namespace NPC, origem e guards no JSON.", "", "| Fonte | Categoria | Tipo | Assinatura | Referência |", "|---|---|---|---|---|"]
    for e in registry["entries"]:
        if e["visibility"] == "public" and e["kind"] in CALLABLES:
            signature = e["signature"].replace("|", "\\|").replace("`", "&#96;")
            lines.append(f'| {e["source_id"]} | {e["category"]} | {e["kind"]} | `{signature}` | [L{e["line"]}]({e["source_url"]}) |')
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, default=ROOT / "build/api-registry-sources")
    parser.add_argument("--lock", type=Path, default=ROOT / "docs/compatibility/sources.lock.json")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/compatibility/catalog.json")
    parser.add_argument("--fetch", action="store_true", help="Download only pinned, hash-checked source text if absent/changed")
    parser.add_argument("--check", action="store_true", help="Compare deterministic output, do not change repository files")
    args = parser.parse_args()
    lock = json.loads(args.lock.read_text(encoding="utf-8-sig"))
    registry = build_registry(lock, load_inputs(lock, args.sources, args.fetch))
    content = json.dumps(registry, ensure_ascii=False, indent=2) + "\n"
    md = markdown(registry)
    if args.check:
        if args.output.read_text(encoding="utf-8") != content or args.output.with_suffix(".md").read_text(encoding="utf-8") != md:
            raise SystemExit("Registry is out of date")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(content, encoding="utf-8", newline="\n")
        args.output.with_suffix(".md").write_text(md, encoding="utf-8", newline="\n")
    print(json.dumps(registry["summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

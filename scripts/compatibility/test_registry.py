"""Tests of inventory integrity; no Pawn compatibility/runtime claims."""
import json
from pathlib import Path
import tempfile
import unittest

import generate_registry as registry


class PawnDeclarationTests(unittest.TestCase):
    def test_comments_and_strings_are_not_declarations(self):
        rows, _ = registry.parse_include('/* native Fake(); */\n// forward OnFake();\nnative Real(const s[] = "native Fake();");')
        self.assertEqual([r["name"] for r in rows], ["Real"])
        self.assertIn('"native Fake();"', rows[0]["signature"])

    def test_multiline_tags_refs_defaults_varargs(self):
        rows, _ = registry.parse_include('native bool:GetPos(playerid,\n &Float:x, const name[] = "a  b", {Float,_}:...);')
        self.assertEqual(rows[0]["name"], "GetPos")
        self.assertIn('"a  b"', rows[0]["signature"])
        self.assertIn("& Float : x", rows[0]["signature"])
        self.assertIn("...", rows[0]["signature"])

    def test_native_alias_is_not_an_extra_host_binding(self):
        rows, _ = registry.parse_include('native PrintF(const s[]) = printf;')
        self.assertEqual(rows[0]["binding"], "printf")

    def test_stock_body_not_inventoried_as_api(self):
        rows, _ = registry.parse_include('stock Wrapper() { new constValue = 5; if (true) { print("native X();"); } return 1; }\nnative Next();')
        self.assertEqual([(r["kind"], r["name"]) for r in rows], [("stock", "Wrapper"), ("native", "Next")])

    def test_static_qualifier_preserved_without_copying_body(self):
        rows, _ = registry.parse_include('static stock Helper(x) { return x; }\nstatic const LIMIT = 7;')
        self.assertTrue(rows[0]["signature"].startswith("static stock"))
        self.assertNotIn("return", rows[0]["signature"])
        self.assertTrue(rows[1]["signature"].startswith("static const"))

    def test_macros_guards_and_conditional_variants(self):
        rows, _ = registry.parse_include('#if defined LEGACY\nnative Foo(x);\n#else\nnative bool:Foo(const x[]);\n#endif\n#define ALIAS(%0) Foo(%0)\n#define LIMIT 0xFFFF\n')
        funcs = [r for r in rows if r["kind"] == "native"]
        self.assertEqual(len(funcs), 2)
        self.assertNotEqual(funcs[0]["conditions"], funcs[1]["conditions"])
        alias = next(row for row in rows if row["name"] == "ALIAS")
        limit = next(row for row in rows if row["name"] == "LIMIT")
        self.assertEqual(alias["kind"], "macro")
        self.assertEqual(limit["kind"], "constant_macro")
        self.assertIn("0xFFFF", limit["signature"])
        self.assertNotIn("0 x", limit["signature"])

    def test_implicit_enum_values_not_guessed(self):
        rows, _ = registry.parse_include('enum TAG { FIRST = 7, SECOND, Float:coords[3], LAST = (1 << 4) };')
        members = [r for r in rows if r["kind"] == "enum_member"]
        self.assertEqual([r["name"] for r in members], ["FIRST", "SECOND", "coords", "LAST"])
        self.assertIsNone(members[1]["value_expression"])

    def test_forward_not_automatically_engine_callback(self):
        rows, _ = registry.parse_include('forward Helper(x); forward OnPlayerConnect(playerid);')
        self.assertEqual([r["kind"] for r in rows], ["forward", "callback"])

    def test_deprecation_attached_to_declaration_not_following_api(self):
        rows, _ = registry.parse_include('#pragma deprecated Broken; use NewAPI\n/** comment */\nstock OldAPI() { return 0; }\nnative NewAPI();')
        self.assertEqual(rows[0]["deprecation_notices"], ["Broken; use NewAPI"])
        self.assertEqual(rows[1]["deprecation_notices"], [])

    def test_public_variable_not_stock(self):
        rows, _ = registry.parse_include('public Streamer_IncludeFileVersion = 0x296;')
        self.assertEqual(rows[0]["kind"], "public_variable")
        self.assertIn("0x296", rows[0]["signature"])

    def test_pawn_operator(self):
        rows, _ = registry.parse_include('native bool:operator>(Float:a, Float:b) = floatcmp;')
        self.assertEqual(rows[0]["name"], "operator>")

    def test_no_silent_skip_of_bad_declaration(self):
        with self.assertRaises(ValueError):
            registry.parse_include('native Unfinished(x')
        with self.assertRaises(ValueError):
            registry.parse_include('#if X\nnative X();')

    def test_source_tampering_rejected_without_fetch(self):
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            (cache / "source").mkdir()
            (cache / "source" / "api.inc").write_bytes(b'native Foo();')
            lock = {"sources": [{"id": "source", "files": [{"path": "api.inc", "sha256": registry.digest(b'native Different();')}]}]}
            with self.assertRaises(ValueError):
                registry.load_inputs(lock, cache, fetch=False)

    def test_source_paths_cannot_escape_cache(self):
        for path in ("../outside.inc", "/absolute.inc", "C:/outside.inc", "a\\outside.inc", "a//b.inc"):
            with self.subTest(path=path), tempfile.TemporaryDirectory() as temp:
                lock = {"sources": [{"id": "source", "files": [{"path": path, "sha256": "0" * 64}]}]}
                with self.assertRaisesRegex(ValueError, "pinned source path"):
                    registry.load_inputs(lock, Path(temp), fetch=False)


class LockedCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads((registry.ROOT / "docs/compatibility/catalog.json").read_text(encoding="utf-8"))

    def test_every_entry_has_contract_planning_fields_and_pending_status(self):
        rows = self.catalog["entries"]
        self.assertEqual(len(rows), len({r["id"] for r in rows}))
        for row in rows:
            for key in ("signature", "category", "source_version", "source_commit", "source_url", "semantics", "grandmp_status", "dependencies", "test_criteria"):
                self.assertTrue(row[key], (row["id"], key))
            self.assertEqual(row["grandmp_status"], "PENDING")
            self.assertEqual(row["implementation_evidence"], [])

    def test_npc_contexts_kept_separate(self):
        legacy = [r for r in self.catalog["entries"] if r["source_id"] == "samp" and r["source_file"] == "a_npc.inc" and r["kind"] == "native"]
        modern = [r for r in self.catalog["entries"] if r["source_id"] == "omp" and r["name"] == "NPC_Create"]
        self.assertTrue(legacy and modern)
        self.assertTrue(all(r["execution_context"] == "npc_script" for r in legacy))
        self.assertTrue(all(r["execution_context"] == "server_script" for r in modern))

    def test_classic_operations_remain_classic_when_kind_changes(self):
        for name in ("ShowPlayerMarkers", "GetPlayerPoolSize", "GetVehiclePoolSize", "GetActorPoolSize", "SetTeamCount", "EnableTirePopping", "SetDeathDropAmount", "print", "printf"):
            with self.subTest(name=name):
                classic = [r for r in self.catalog["entries"] if r["source_id"] == "samp" and r["name"] == name and r["execution_context"] == "server_script"]
                modern = [r for r in self.catalog["entries"] if r["source_id"] == "omp" and r["name"] == name and r["visibility"] == "public" and r["execution_context"] == "server_script" and r["origin_family"] != "PAWN_STDLIB"]
                self.assertTrue(classic and modern)
                self.assertTrue(all(r["origin_family"] == "OPENMP_VARIANT_OF_SAMP" for r in modern))
                self.assertTrue(all(r["grandmp_status"] == "PENDING" for r in modern))

    def test_deprecated_pool_size_stocks_are_not_verified_doc_behavior(self):
        for name in ("GetPlayerPoolSize", "GetVehiclePoolSize", "GetActorPoolSize"):
            row = next(r for r in self.catalog["entries"] if r["source_id"] == "omp" and r["kind"] == "stock" and r["name"] == name)
            self.assertTrue(any("broken" in note.lower() for note in row["deprecation_notices"]))
            self.assertTrue(row["semantics"]["variant_caution"])
            self.assertEqual(row["semantics"]["review_state"], "UNREVIEWED")

    def test_essential_profile_names_are_real_and_scoped(self):
        profile = json.loads((registry.ROOT / "docs/compatibility/essential-profile.json").read_text(encoding="utf-8"))
        for requirement in profile["requirements"]:
            found = [r for r in self.catalog["entries"] if r["source_id"] == profile["source_id"] and r["execution_context"] == "server_script" and r["name"] == requirement["name"] and r["kind"] == requirement["kind"]]
            self.assertTrue(found, requirement)


if __name__ == "__main__":
    unittest.main()

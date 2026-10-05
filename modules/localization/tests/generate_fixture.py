"""Generate native/wasm test bytes through the production cooker, not a mock codec."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts/python"))
from localization import bindings, cook, document, export_review, read_json, write_atomic

source = document(read_json(Path(__file__).parent / "source.json"), False)
translation = export_review(source, "fr-FR")
translated = {"empty": "", "menu/play": "Jouer", "literal": "L'apostrophe {player} reste littérale.\n"}
for message in translation["messages"]:
    message["text"] = translated.get(message["key"], message["source_text"])
    message["status"] = "approved"
fallback = export_review(source, "ja-JP")
lines = [bindings(source, "localization_fixture")]
for name, value in (("kSourceCatalog", cook(source)), ("kFrenchCatalog", cook(source, translation)),
                    ("kFallbackCatalog", cook(source, fallback, allow_source_fallback=True))):
    lines += [f"inline constexpr ludus::foundation::uint8 {name}[] = {{"]
    for start in range(0, len(value), 16):
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in value[start:start + 16]) + ",")
    lines.append("};\n")
write_atomic(Path(sys.argv[1]), "\n".join(lines).encode("utf-8"))

"""Export both complete native actor profiles from the original Dreamcast image."""

import argparse
import json
from pathlib import Path

from ed_actor_textures import export_actor_textures
from ed_dc_audio import checked_output
from export_actor import ACTORS, LEVEL_B_ACTORS, export_actor


def export_profiles(source, output):
    source = Path(source).resolve()
    output = checked_output(source, output)
    if output.exists():
        raise FileExistsError("Dreamcast actor profiles require a new isolated output")
    output.mkdir(parents=True)
    pending = output / "INCOMPLETE"
    pending.write_text("Original actor export is incomplete.\n", encoding="ascii")
    result = {"format": "EDDCACTORS1", "source": str(source), "levels": {}}
    for level, definitions in (("A", ACTORS), ("B", LEVEL_B_ACTORS)):
        directory = output / level
        directory.mkdir()
        actors = {name: export_actor(source, directory, name, definition=definition, dreamcast=True)
                  for name, definition in definitions.items()}
        textures = export_actor_textures(source, directory,
                                        {name: d["texture"] for name, d in definitions.items()}, dreamcast=True)
        result["levels"][level] = {"actors": actors, "textures": len(textures["textures"])}
    (output / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    pending.unlink()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = export_profiles(args.source, args.output)
        print(json.dumps({k: len(v["actors"]) for k, v in result["levels"].items()}))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Error: {error}\n")

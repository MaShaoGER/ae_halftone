"""Embed runtime-compiled Metal source without requiring the Xcode Metal tools."""
from pathlib import Path


def embed_metal(root: Path, build: Path):
    source = (root / "src/RasterMetal.metal").read_text()
    shared = (root / "src/RasterShared.h").read_text().replace("#pragma once", "")
    source = source.replace('#include "RasterShared.h"', shared)
    build.mkdir(parents=True, exist_ok=True)
    (build / "RasterMetalSource.h").write_text(
        '#pragma once\nstatic constexpr const char* rasterMetalSource = R"OP_METAL(\n'
        + source + '\n)OP_METAL";\n'
    )


if __name__ == "__main__":
    import sys
    embed_metal(Path(sys.argv[1]), Path(sys.argv[2]))

"""parshred — The World's Fastest XML Parser."""

try:
    from ._parshred import (
        SaxParser,
        iterparse,
        iterparse_string,
        load,
        ParseError,
        IOError,
        SecurityError,
        __version__,
    )
except ImportError:
    # C++ extension not built yet
    __version__ = "0.1.0"
    
    def _not_built(*args, **kwargs):
        raise RuntimeError(
            "parshred C++ extension not built. "
            "Run: pip install -e . (from the project root)"
        )
    
    SaxParser = _not_built
    iterparse = _not_built
    iterparse_string = _not_built
    load = _not_built

__all__ = [
    "SaxParser",
    "iterparse",
    "iterparse_string",
    "load",
    "__version__",
]

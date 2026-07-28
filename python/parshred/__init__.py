"""parshred -- High-performance SIMD-accelerated XML parser."""

try:
    from ._parshred import (
        Document,
        Element,
        parse,
        parse_file,
        validate,
    )
except ImportError as e:
    _import_error = e

    def _not_built(*args, **kwargs):
        raise RuntimeError(
            "parshred C++ extension not available. "
            "Install with: pip install parshred\n"
            f"Import error was: {_import_error}"
        )

    parse = _not_built
    parse_file = _not_built
    validate = _not_built
    Document = None
    Element = None

__version__ = "0.1.0"

__all__ = [
    "Document",
    "Element",
    "parse",
    "parse_file",
    "validate",
    "__version__",
]

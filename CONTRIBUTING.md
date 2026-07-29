# Contributing to parshred

## Bug reports

Open a GitHub Issue and include:

- Minimal XML input that reproduces the problem.
- Expected output vs. actual output.
- Platform details (OS, compiler, CPU, parshred version).
- Error messages or stack traces if any.

## Pull requests

1. Fork the repository.
2. Create a branch for your change.
3. Make your changes, following the style below.
4. Ensure all tests pass.
5. Submit a pull request with a clear description.

By submitting a PR, you agree to license your contribution under the project's dual license (AGPL-3.0 / Commercial). The CLA Assistant bot will post a link on your first PR — click and confirm, no paperwork. See [`docs/cla/`](docs/cla/) for the CLA text.

## Development setup

```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug
```

## Code style

- Use C++20.
- Follow existing patterns in the codebase.
- Avoid adding comments unless they are genuinely needed.
- Keep changes focused.

## Testing

- All PRs must pass the existing test suite.
- New features must include tests.
- Run the full test suite before submitting.

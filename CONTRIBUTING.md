# Contributing

MimiClaw is maintained as a Linux / WSL native C project.

Before sending changes:

```bash
cmake -S . -B build
cmake --build build -j
```

Keep documentation aligned with the Linux-only runtime model and avoid reintroducing ESP-IDF-specific files, scripts, or instructions.

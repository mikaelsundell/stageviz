# Write Python for Stageviz

New to the application API? Start with the [Python guide](references/python-start.md).
It explains execution, selection, layers, commands, transforms, saving and tools
without requiring example scripts.

| You need | Read |
| --- | --- |
| Your first script and the editing model | [Start writing Python](references/python-start.md) |
| Exact methods, arguments, return values and constants | [Python API reference](references/python-api.md) |
| A dialog, dock, shelf UI or material tool | [Dialogs and panels](references/python-dialogs.md) |
| What Python exposes compared with native C++ | [Binding boundaries](references/python-binding-coverage.md) |
| Repeatable embedded Python/Qt verification | [Qt testing](references/python-qt-testing.md) |
| Instructions for a coding agent | [Stageviz Python skill](SKILL.md) |

For an agent, point directly to [SKILL.md](SKILL.md) and ask it to follow the
linked API reference before writing Stageviz calls. A GitHub directory URL alone
does not guarantee that an agent loads its files as instructions. For a human,
the guide and reference are ordinary documentation; installing a skill is optional.

These documents describe the bindings in this checkout. An older installed app
may expose fewer APIs. All Stageviz calls run inside its embedded interpreter;
`stageviz` is not a standalone pip package. OpenUSD and Qt remain separate APIs.

Maintainers: run `python3 CTest/check_python_documentation.py` after changing
bindings or the reference. It checks registration/signature coverage and enum
membership without launching Stageviz; runtime behavior needs its own tests.

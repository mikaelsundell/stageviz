# Stageviz MCP server

Stageviz can expose its active session to MCP clients through a local
Streamable HTTP endpoint hosted inside the application. The server binds only
to `127.0.0.1` and is disabled by default.

## Enable it

In Stageviz's Python console, start the server:

```python
stageviz.startMcpServer()
```

This returns the endpoint URL (`http://127.0.0.1:37651/mcp`). The server remains
active until Stageviz exits or you stop it with:

```python
stageviz.stopMcpServer()
```

Check its state with `stageviz.isMcpServerRunning()`. The server is not started
automatically when Stageviz launches.

## Connect from Codex

Add the endpoint once:

```sh
codex mcp add stageviz --url http://127.0.0.1:37651/mcp
```

Start a new Codex session while Stageviz is running.

## Connect from Claude Code

With Stageviz open and its MCP server running, add the local HTTP endpoint to
Claude Code:

```sh
claude mcp add --transport http --scope user stageviz http://127.0.0.1:37651/mcp
```

The `user` scope makes the server available across Claude Code projects. Open a
Claude Code session and run `/mcp` to check that Stageviz is connected and its
tools are listed. See the [Claude Code MCP guide](https://code.claude.com/docs/en/mcp)
for other configuration options.

Claude Code runs on the same computer as Stageviz, so it can reach this local
endpoint.

### Claude Desktop

Claude Desktop's custom remote connectors connect from Anthropic's cloud, so
they cannot reach Stageviz's loopback-only `127.0.0.1` endpoint. The local
MCP configuration in Claude Desktop is a separate mechanism; Stageviz currently
provides an HTTP endpoint for local clients such as Claude Code. See Claude's
[custom connector network requirements](https://support.claude.com/en/articles/11175166-get-started-with-custom-connectors-using-remote-mcp).

## Available tools

- `stageviz_get_stage_info` — active stage, file, root layer, default prim,
  stage up axis and layer count.
- `stageviz_list_prims` — bounded hierarchy listing with depth and count limits.
- `stageviz_get_prim_info` — prim composition state, attributes and relationships.
- `stageviz_get_selection` and `stageviz_set_selection` — read or replace the
  current selection.
- `stageviz_run_python` — run source code in Stageviz's embedded Python
  interpreter and return captured stdout/stderr.

Python execution can make direct USD edits and clears the native undo history
before it runs, matching Stageviz's existing Python execution behavior. Prefer
the named inspection and selection tools when they are sufficient.

Tool requests are handled on Stageviz's Qt application thread so calls operate
on the active session. Keep Python calls short; a long-running script also
blocks the UI while it runs.

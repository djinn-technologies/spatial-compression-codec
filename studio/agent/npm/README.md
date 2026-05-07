# @djinn/scc-studio-agent

Native daemon owning the depth-sensor connection for the SCC Studio.
Distributed as platform-matched binaries inside this npm package; the
JavaScript shim execs the right one for the host.

## Install

```bash
npm install -g @djinn/scc-studio-agent
scc-studio-agent --help
```

The CLI launches the agent as a foreground process. The Studio
frontend connects to its IPC socket and drives capture sessions.

## Privacy notice

**The agent is the only component in the SCC stack that can see raw
sensor depth data.** It owns the `/dev/video*` (or platform-equivalent)
handle, performs the encode in-process via libscc, and emits the
compressed SEI bytes over its IPC socket. The Studio frontend never
sees the raw depth; it sees only the post-encode bytes.

This separation is by design: the agent runs as the trusted boundary
between the OS-level sensor permission and the rest of the system.
A hostile / compromised Studio frontend cannot exfiltrate raw depth
bytes; the worst it can do is request more frames than it should and
discard them.

## License

Apache-2.0.

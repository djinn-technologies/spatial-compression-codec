# SCC — Build Documentation Bundle

**Spatial Compression Codec (SCC)** — comprehensive build specification produced by the Imhotep skill (Anthropic Claude) on behalf of **Akuma Engineering Ltd. — Architecture Practice**, for **Djinn Technologies Ltd.**, the assignee of US Patent 10,827,161 B2.

This bundle is a **forward-looking build specification** — Imhotep's standard repository-scanning workflow has been adapted to a greenfield context. The "cite or die" doctrine is preserved by anchoring every architectural claim to one of three evidence sources: the patent text, the numbered requirements derived from the originating brief, or a published industry standard.

---

## Bundle contents

```
scc-sad/
├── Solutions_Architecture_Document_SCC.docx   ← Primary branded deliverable
├── SAD.md                                     ← Markdown source of truth
├── README.md                                  ← This file
├── imhotep_state.json                         ← Build provenance
│
├── diagrams/                                  ← 19 diagrams x 4 formats
│   ├── 01..19_*.mmd                           ← Mermaid sources (canonical)
│   ├── 01..19_*.dot                           ← Graphviz sources (rendered)
│   ├── 01..19_*.drawio                        ← draw.io / diagrams.net editable
│   └── 01..19_*.png                           ← Rendered for DOCX embed
│
├── prompts/
│   └── AI_Build_Prompts.md                    ← 15 Claude Code prompts
│
└── acceptance/
    ├── Acceptance_Criteria.md                 ← REQ-001..REQ-092 catalogue
    └── Test_Plan.md                           ← Comparator harness + tiers
```

## Reading order

1. **`Solutions_Architecture_Document_SCC.docx`** — branded for distribution to internal leadership and counsel.
2. **`SAD.md`** — same content, for engineers who want diff-able review and inline editing.
3. **`acceptance/Acceptance_Criteria.md`** — what "done" means, REQ by REQ.
4. **`acceptance/Test_Plan.md`** — how each REQ is validated.
5. **`prompts/AI_Build_Prompts.md`** — the build kit, prompt-by-component, ready for Claude Code.

## Diagram editing

Each diagram ships in three editable formats — pick the one that suits your environment:

- **Mermaid (`.mmd`)** — paste into VS Code with the Mermaid extension, GitHub Markdown, Obsidian, Notion, or any Mermaid-aware tool.
- **Graphviz (`.dot`)** — open in any graphviz tool; render with `dot -Tsvg foo.dot -o foo.svg`.
- **draw.io (`.drawio`)** — open at https://app.diagrams.net; node positions are scaffolded, re-layout for production polish.

The `.png` files are the rendered versions embedded in the DOCX.

---

*— Imhotep skill, Anthropic Claude, on behalf of Akuma Engineering Ltd. — Architecture Practice. Generated 2026-05-06.*

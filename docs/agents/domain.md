# Domain docs

This document tells engineering skills how to consume this repository's domain documentation.

## Before exploring

Before exploring a feature area, read the relevant documents when they exist:

- `CONTEXT.md` at the repository root.
- `docs/adr/` for architectural decisions that affect the area.

If either location does not exist, continue silently. Create domain documentation only when the project resolves terminology or an architectural decision that merits recording.

## Layout

This is a single-context repository:

```text
/
├── CONTEXT.md
├── docs/
│   └── adr/
└── examples/
```

## Vocabulary

Use the terminology defined in `CONTEXT.md` in issue titles, specifications, test names, implementation proposals, and code comments. If a needed concept is absent, either avoid introducing a needless synonym or record the gap for domain modeling.

## ADR conflicts

If a proposed change conflicts with an existing ADR, state that conflict explicitly rather than silently overriding the decision.

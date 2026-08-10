# Issue tracker: Local Markdown

Issues and specs for this repository live as Markdown files in `.scratch/`.

## Conventions

- One feature per directory: `.scratch/<feature-slug>/`.
- The feature specification is `.scratch/<feature-slug>/spec.md`.
- Implementation issues are one file per ticket at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`; do not create one combined tickets file.
- Record triage state in a `Status:` line near the top of each issue file. See `triage-labels.md` for the canonical role strings.
- Append comments and conversation history under a `## Comments` heading.

## Publishing work

When a skill says to publish to the issue tracker, create the appropriate file under `.scratch/<feature-slug>/`, creating directories as needed.

## Reading work

When a skill says to fetch a ticket, read the referenced `.scratch/` file. The user will normally provide its path or issue number.

## Wayfinding operations

Used by the `wayfinder` skill:

- **Map:** `.scratch/<effort>/map.md` records notes, decisions, and remaining uncertainty.
- **Child ticket:** `.scratch/<effort>/issues/<NN>-<slug>.md` contains one question or task. Use a `Type:` line (`research`, `prototype`, `grilling`, or `task`) and a `Status:` line (`claimed` or `resolved`).
- **Blocking:** Record blockers with `Blocked by: NN, NN` near the top. A ticket is unblocked only when every listed ticket is resolved.
- **Frontier:** Scan `.scratch/<effort>/issues/` for open, unblocked, unclaimed tickets; work in numeric order.
- **Claim:** Set `Status: claimed` before starting work.
- **Resolve:** Add an `## Answer` section, set `Status: resolved`, then add a context pointer to the map's decisions section.

# Triage labels

Engineering skills use five canonical triage roles. This file maps them to the status strings used by this repository's local Markdown issue tracker.

| Skill role | Local tracker status | Meaning |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | A maintainer needs to evaluate the issue. |
| `needs-info` | `needs-info` | Waiting for more information from the reporter. |
| `ready-for-agent` | `ready-for-agent` | Fully specified and ready for an autonomous agent. |
| `ready-for-human` | `ready-for-human` | Requires human implementation or a human decision. |
| `wontfix` | `wontfix` | Will not be actioned. |

When a skill refers to one of these roles, use the matching value in the `Status:` line of the relevant `.scratch/` issue file.

## Review guidelines

Scope
- One milestone per PR. If a PR mixes work from multiple milestones or adds unrelated refactors, request a split.
- Keep changes focused on the stated deliverable for the milestone branch.
- Small, safe fixes are fine when they are directly in support of the milestone and do not broaden scope materially.

Milestone guides
- Use the milestone guide under `docs/milestones/` that matches the PR branch or base branch when one of the following branch names appears:
  - `feature/milestone-1-benchmark` -> `docs/milestones/Milestone1_HostSideTestHarness.md`
  - `feature/milestone-2-tinyframe` -> `docs/milestones/Milestone2_TinyFrame.md`
  - `feature/milestone-3-crc-framing` -> `docs/milestones/Milestone3_LengthPrefixCRC.md`
  - `feature/milestone-4-cdc` -> `docs/milestones/Milestone4_CDCWriteOptimization.md`
- Treat the matching milestone guide as the primary checklist for scope and expected deliverables.
- If no milestone guide matches the PR branch or base branch, review against the PR description and the code diff directly.

Correctness
- Confirm the PR satisfies the milestone deliverables it claims to implement.
- Flag behavioral changes outside the stated milestone scope.
- Ensure build, benchmark, and protocol/documentation changes remain consistent with the implementation.
- Be explicit about unsupported commands, partial implementations, benchmark gaps, and protocol mismatches.
- When raising a finding, prefer to also suggest a concrete fix, preferred resolution, or next verification step so the review is actionable instead of purely critical.

Commit/history hygiene
- Commits should be focused and reviewable.
- Messages should state what changed and why.
- If the branch is not rebased onto its base branch, request a rebase before merge.

When to request changes
- Mixed milestone scope
- Missing or incomplete milestone deliverables
- Unclear or unsafe behavior changes
- Build or review workflow regressions
- Noisy history or unclear commit intent

Review style
- Findings should be specific and technically grounded.
- Prefer actionable review comments over purely descriptive criticism.
- When possible, point to the likely remediation path, not just the symptom.

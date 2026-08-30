# CLAUDE.md

Read `AGENTS.md` first. It is the maintainer's guide for this repository
(project purpose, architecture, build constraints, coding style, what not to
do, and where to start). Everything that governs the work is there or in the
documents it points at; this file carries only what is specific to Claude Code.

## Project memory (repo-local)

Cross-session working memory for this repo lives in `.claude/memory/`
(git-ignored). It does not load into context automatically, so:

- At the start of a session, read `.claude/memory/MEMORY.md` (the index) and
  any entry it points to that is relevant to the task.
- Record new or corrected memories as files in `.claude/memory/` (one fact per
  file, keeping the existing frontmatter convention) and add a one-line pointer
  to `.claude/memory/MEMORY.md`. Do not write memories anywhere else.

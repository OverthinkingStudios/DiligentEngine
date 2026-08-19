# Catalog agent prompts (Phase 1)

One prompt file per Concept Catalog subsystem (task.md §5). Each is a complete, self-contained
instruction for a cold-context general-purpose agent. To (re)launch: pass the file content
verbatim as the agent prompt. Agents write `port_effort_3/catalog/<name>.md` and nothing else;
they never commit.

Status tracking: a subsystem is DONE when its `catalog/<name>.md` exists, was reviewed by the
main agent, and is committed. Check `git log --oneline -- port_effort_3/catalog/` for progress.

| Prompt file | Deliverable | 
|---|---|
| terrain_quadtree_streaming.txt | catalog/terrain_quadtree_streaming.md |
| gpu_tile_pipeline.txt | catalog/gpu_tile_pipeline.md |
| vegetation.txt | catalog/vegetation.md |
| roads_terrafectors.txt | catalog/roads_terrafectors.md |
| atmosphere_shadows.txt | catalog/atmosphere_shadows.md |
| shader_interface.txt | catalog/shader_interface.md |
| conventions_app_wiring.txt | catalog/conventions_app_wiring.md |

Launch economically (2-3 at a time) — each agent burns roughly 100-250k tokens; the full set
twice already died to session rate limits (2026-08-02).

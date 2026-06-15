# Earthworks Terrain Sample (Phase 1)

Minimal Diligent sample that bakes a single static LOD-0 terrain tile through the
EarthworksFX compute chain (bicubic → normals → adaptive vertices → jump flood →
delaunay) and draws it with an indirect draw built by `compute_tileBuildLookup`.

- Procedural heightmap (no external data required).
- Simplified pixel shader: directional sun + normal map + ambient.
- WASD + mouse to fly the camera.

See `docs/porting/KICKOFF.md` section 7 for the phase-1 milestone and acceptance.

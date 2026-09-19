# Task — fix/streamline-nr-path-and-depth-srv

Plan: memory/plans/2026-09-19-port-streamline-nr-path-and-depth-srv.md. Ledger: wilsjo2-fork:streamline-plugin-binding.

## 2026-09-20 — ported and built

Branch cut from `main` (`886200f9`). Two commits, authored as wilsjo2, `Ported-from:` trailers: `7df67eed` b3618c05 (nvngx_dlssnr.dll directory added to Streamline's NGX search paths), `733c2db0` 7543d143 (Shader_Dx12.cpp: keep R32_FLOAT_X8X24_TYPELESS for SRVs). Release x64 built, exit 0. Smoke test not ported.

Pending: push and PR (user says when); a Streamline game with NR on and the runtime beside the exe (search path); a game whose NR depth guide is R32G8X24 (depth SRV).

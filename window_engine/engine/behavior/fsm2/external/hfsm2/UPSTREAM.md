# HFSM2 (vendored)

Vendored verbatim from https://github.com/andrew-gresyk/HFSM2

- **Version:** 2.12.1 (2026-06-11) — single-header distribution `machine.hpp`
- **License:** MIT (see `LICENSE`)

`machine.hpp` is the unmodified upstream `include/hfsm2/machine.hpp`. Do **not**
edit it locally. To update, replace `machine.hpp` + `LICENSE` from a tagged
release and re-run `behavior_fsm2_behavior_tests`.

It is used only by project behaviors that include
`<engine/behavior/fsm2/fsm2_behavior.h>`. HFSM2 is header-only: nothing links it,
and the engine binaries (`wozzits_abi` / `wozzits_app_v1` / `wozzits_editor_host`)
do not include it, so adding it needs no ABI bump and no engine relink.

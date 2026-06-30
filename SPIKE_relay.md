# Step 0 spike: cross-split absolute pipeline set + persist

Goal: prove the riskiest assumption behind the trackball config app — that an
**absolute** pipeline selection with a **persist flag** can be carried across
the split to the **left (peripheral)** ball, applied, and survive a reboot.
This is the same relay path the central-side Config Service will use later.

## What changed for the spike

- `zmk-input-processor-pipeline-switch`: added `zip_pipeline_switch_set(dev,
  index, persist)` (absolute set; `persist=false` = RAM-only live preview).
  The `zmk,behavior-pipeline-switch` behavior now takes **2 cells**: `param1`
  = target index, `param2` = persist (0/1). (Cycle remains as a C API.)
- `crosses_v2.dts`: `pipe_switch` is now `#binding-cells = <2>`.
- `crosses_v2.keymap`: **SPIKE bindings** on two left-half mouse-layer keys —
  `&pipe_switch 1 1` (mouse, persist) and `&pipe_switch 0 1` (scroll, persist).
  Revert before merge.

## Procedure (needs both halves flashed)

1. Build + flash **both** halves from the `vinnie/gatt` branch.
   (Right half is central w/ the `studio-rpc-usb-uart` snippet.)
2. Hold the mouse layer and press the **left-half** key bound to
   `&pipe_switch 1 1`. The **left** ball should switch to **mouse** behavior
   (no scroll mapping). Press the neighbor (`&pipe_switch 0 1`) → back to
   **scroll**. This confirms: param-carrying relay + absolute set applied on
   the peripheral.
3. Set it to **mouse**, then **power-cycle** the left half. After boot the
   left ball should still be in **mouse** mode → confirms per-side persist of
   the absolute index (settings key `zip_ps/<n>` on the peripheral).
4. (Optional, isolates the relay) watch RTT/USB logs on the peripheral for
   `... active pipeline now 1` from `LOG_INF` in the pipeline-switch.

## Pass criteria

- [ ] Left ball changes behavior on keypress (relay + absolute set works).
- [ ] Selection persists across a peripheral power-cycle.
- [ ] No errors in log; behavior name `pswitch` resolves on the peripheral.

## What this does NOT yet test (next, when wiring the Config Service)

The spike triggers the relay from a **keypress** (event.source = the physical
left key's peripheral). The Config Service will instead **originate** the same
call from central code on a GATT write:

```c
struct zmk_behavior_binding b = { .behavior_dev = "pswitch",
                                  .param1 = index, .param2 = persist };
struct zmk_behavior_binding_event e = { .source = 0 /* left peripheral */ };
zmk_behavior_invoke_binding(&b, e, true);
zmk_behavior_invoke_binding(&b, e, false);
```

Per ZMK `behavior.c`, `EVENT_SOURCE` locality routes `source == 255`
(`ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL`) to run locally on the central and
any `0..N-1` to `zmk_split_central_invoke_behavior(source, ...)`. So the right
ball uses `.source = 255`, the left ball `.source = 0`. The remaining unknown
is only invoking outside a keypress context (timestamp/locking) — cheap to
confirm with a temporary central-locality test key if the keypress spike
passes.

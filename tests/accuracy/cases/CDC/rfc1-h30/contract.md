# rfc1-h30 contract

## Summary

A 16x8 buffer memory is written and read by logic in a single clock domain. Requests are registered in one module and applied to a memory module with a registered read port.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

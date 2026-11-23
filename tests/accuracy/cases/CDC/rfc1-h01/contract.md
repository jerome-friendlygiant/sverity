# rfc1-h01 contract

## Summary

A sample register is clocked by a locally divided clock derived from the system clock. The constraint file describes the divided clock as a generated clock. The output is launched by the divided clock.

## Clocks

- `clk_sys`: period 10.0 ns, top-level port `clk_sys`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

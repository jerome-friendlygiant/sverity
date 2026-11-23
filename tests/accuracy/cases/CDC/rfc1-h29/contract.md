# rfc1-h29 contract

## Summary

An event counter produces a Gray-coded pointer that passes through a two-stage pipeline before being decoded and compared against a local reference count.

## Clocks

- `clk_sys`: period 10.0 ns, top-level port `clk_sys`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

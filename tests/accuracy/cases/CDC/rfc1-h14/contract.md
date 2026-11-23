# rfc1-h14 contract

## Summary

A 4-bit counter in clk_a is converted to Gray code and presented to clk_b through per-bit register stages. The synchronized pointer is an output.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`
- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

# rfc1-h02 contract

## Summary

A registered input bit in clk_a is passed to clk_b through a two-register chain and an output register. The constraint file describes the relationship between the two clocks.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`
- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

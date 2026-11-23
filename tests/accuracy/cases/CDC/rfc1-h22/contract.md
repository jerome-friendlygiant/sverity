# rfc1-h22 contract

## Summary

The part runs from either a fast clock or a slow clock, selected at power-up by the board, never both. A bit registered in the fast-clock logic is read by the slow-clock logic.

## Clocks

- `clk_fast`: period 4.0 ns, top-level port `clk_fast`
- `clk_slow`: period 10.0 ns, top-level port `clk_slow`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

# rfc1-h03 contract

## Summary

A status flag register in the clk_a block is read by a receiver block clocked by clk_b. The receiver output feeds a top-level port.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`
- `clk_b`: period 12.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

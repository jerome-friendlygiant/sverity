# rfc1-h07 contract

## Summary

An 8-bit configuration word is registered in src_clk and consumed by a register in dst_clk, then presented on an output.

## Clocks

- `src_clk`: period 10.0 ns, top-level port `src_clk`
- `dst_clk`: period 7.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

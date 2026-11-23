# rfc1-h19 contract

## Summary

One primary input (timed against src_clk) is registered in both the src_clk and the dst_clk domains and each copy is presented on its own output.

## Clocks

- `src_clk`: period 10.0 ns, top-level port `src_clk`
- `dst_clk`: period 6.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

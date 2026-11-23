# rfc1-h26 contract

## Summary

A slow-domain control bit is brought into the fast domain. The fast-domain logic keeps a true and an inverted shadow copy and combines them into a registered output.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 8.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

# rfc1-h40 contract

## Summary

A sender in src_clk raises a request with an 8-bit payload and holds both until acknowledged. The receiver in dst_clk captures the payload when the request is seen, acknowledges, and the request is released after the acknowledge returns.

## Clocks

- `src_clk`: period 10.0 ns, top-level port `src_clk`
- `dst_clk`: period 8.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

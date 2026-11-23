# rfc1-h05 contract

## Summary

A source-domain bit reaches clk_b through two registers. The consumer side provides a normal output and a faster-reacting output and a mode-gated register.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`
- `clk_b`: period 8.0 ns, top-level port `clk_b`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

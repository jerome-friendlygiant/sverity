# rfc1-h10 contract

## Summary

A request pulse arrives on a primary input from an upstream block clocked by src_clk (the upstream clock port is provided for constraints). The pulse is taken into dst_clk, which runs at 10 ns. The interface description gives the minimum pulse width and requires every pulse to be seen.

## Clocks

- `src_clk`: period 20.0 ns, top-level port `src_clk`
- `dst_clk`: period 10.0 ns, top-level port `dst_clk`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).
- `events.json`: interface contract for primary input `req_pulse_i` (launch clock `src_clk`, protocol `event`, minimum assertion 3 ns, delivery `required`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

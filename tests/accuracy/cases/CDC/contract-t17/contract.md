# CDC-RFC1-T17 interface contract

Only the final synchronizer stage and later pipeline stages feed logic; no ASIC attribute contract is required. Latency is acceptable.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

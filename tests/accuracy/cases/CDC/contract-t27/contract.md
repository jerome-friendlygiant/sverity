# CDC-RFC1-T27 interface contract

Asynchronous level is stable for at least two destination periods; delivery is required. It is not a one-shot pulse.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

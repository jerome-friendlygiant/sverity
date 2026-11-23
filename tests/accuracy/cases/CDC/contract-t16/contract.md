# CDC-RFC1-T16 interface contract

Stable level status; destination accepts latency of two destination cycles. Physical placement/MTBF remains an implementation obligation.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

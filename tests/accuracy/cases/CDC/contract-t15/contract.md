# CDC-RFC1-T15 interface contract

One-cycle asynchronous event is required to be delivered exactly once; there is no source handshake or stretch guarantee.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

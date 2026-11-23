# CDC-RFC1-T21 interface contract

Short event may be missed; system treats this as telemetry only and has no delivery guarantee.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

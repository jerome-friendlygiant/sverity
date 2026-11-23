# CDC-RFC1-T09 interface contract

Source may toggle twice between adjacent destination edges. The edge detector is structurally sound but this rate permits cancellation and lost events.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

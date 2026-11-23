# CDC-RFC1-T24 interface contract

rst_n asserts asynchronously; local_rst_n deasserts only after two dst_clk edges. Recovery/removal and synchronizer placement are external physical obligations.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

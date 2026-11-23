# CDC-RFC1-T05 interface contract

src_payload is a changing multi-bit bus. Valid is synchronized but does not qualify the capture; consumer treats dst_payload as a valid transaction when v2 is high.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

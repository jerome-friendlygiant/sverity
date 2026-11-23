# CDC-RFC1-T14 interface contract

gate_en is controlled by dst_clk and can change while src_clk is high. Intended clock gating needs an integrated glitch-free latch-based cell and characterized clock model.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

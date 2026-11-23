# CDC-RFC1-T26 interface contract

src_data is a free-running source-domain level; SDC binds it to src_clk with min/max input delays. Since it can transition frequently, this control is boundary intent only and CDC-013 event-rate disclosure may remain.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

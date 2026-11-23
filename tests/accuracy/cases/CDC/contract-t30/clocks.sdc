create_clock -name macro_clk -period 12 [get_ports macro_clk]
create_clock -name dst_clk -period 14 [get_ports dst_clk]
set_clock_groups -asynchronous -group [get_clocks macro_clk] -group [get_clocks dst_clk]

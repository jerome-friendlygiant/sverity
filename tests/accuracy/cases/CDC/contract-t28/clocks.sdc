create_clock -name src_clk -period 10 [get_ports src_clk]
create_clock -name dst_clk -period 14 [get_ports dst_clk]
set_clock_groups -asynchronous -group [get_clocks src_clk] -group [get_clocks dst_clk]
create_clock -name div -period 20 [get_ports div]

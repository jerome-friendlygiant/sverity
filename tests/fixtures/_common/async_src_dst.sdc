create_clock -name src_clk_i -period 10 [get_ports src_clk_i]
create_clock -name dst_clk_i -period 7 [get_ports dst_clk_i]
set_clock_groups -asynchronous -group {src_clk_i} -group {dst_clk_i}

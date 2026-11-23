set_units -time ns
create_clock -name src_clk -period 20.0 [get_ports src_clk]
create_clock -name dst_clk -period 8.0 [get_ports dst_clk]
set_clock_groups -asynchronous -group {src_clk} -group {dst_clk}
set_input_delay -clock src_clk 2.0 [get_ports {a_i b_i}]
set_output_delay -clock dst_clk 1.0 [get_ports {both_o}]

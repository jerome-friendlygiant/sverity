create_clock -name src_clk -period 10 [get_ports src_clk]
create_clock -name dst_clk -period 10 [get_ports dst_clk]
set_clock_groups -asynchronous -group {src_clk} -group {dst_clk}
set_input_delay -min 0.1 -clock dst_clk [get_ports req]
set_input_delay -max 1.5 -clock src_clk [get_ports req]
set_input_delay -add_delay -max 0.2 -clock src_clk [get_ports req]

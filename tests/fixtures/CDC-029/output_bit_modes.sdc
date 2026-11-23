create_clock -name src_clk -period 8 [get_ports src_clk]
create_clock -name dst_clk -period 10 [get_ports dst_clk]
set_clock_groups -asynchronous -group {src_clk} -group {dst_clk}
set_input_delay -min 1 -clock src_clk [get_ports {data[0]}]
set_input_delay -max 1 -clock dst_clk [get_ports {data[0]}]
set_input_delay -add_delay -min 1 -clock src_clk [get_ports {data[1]}]
set_input_delay -min 1 -clock dst_clk [get_ports {data[1]}]
set_output_delay 1 -clock dst_clk [get_ports result]

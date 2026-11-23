create_clock -name src -period 10 [get_ports clk_src]
create_clock -name dst -period 12 [get_ports clk_dst]
set_clock_groups -asynchronous -group src -group dst
set_input_delay -clock src 1 [get_ports data]
set_output_delay -clock dst -min 1 [get_ports {observed[0] observed[1]}]
set_output_delay -clock dst -min 1 [get_ports {observed[2]}]
set_output_delay -clock dst -max 2 [get_ports observed]

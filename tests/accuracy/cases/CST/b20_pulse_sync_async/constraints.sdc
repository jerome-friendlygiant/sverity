create_clock -name src_clk -period 10.000 [get_ports clk_src_i]
create_clock -name dst_clk -period 7.000 [get_ports clk_dst_i]
set_clock_groups -asynchronous -group {src_clk} -group {dst_clk}
set_input_delay  -clock src_clk -min 1.000 [get_ports src_pulse_i]
set_input_delay  -clock src_clk -max 3.000 [get_ports src_pulse_i]
set_output_delay -clock dst_clk -min 0.500 [get_ports dst_pulse_o]
set_output_delay -clock dst_clk -max 2.000 [get_ports dst_pulse_o]

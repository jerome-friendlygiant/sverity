create_clock -name source -period 10 [get_ports src_clk]
create_clock -name destination -period 10 [get_ports dst_clk]
set_input_delay -clock source 2 [get_ports value]
set_output_delay -clock destination 2 [get_ports result]

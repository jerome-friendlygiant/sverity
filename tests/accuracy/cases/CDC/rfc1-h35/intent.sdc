set_units -time ns
create_clock -name clk_a -period 20.0 [get_ports clk_a]
create_clock -name clk_b -period 8.0 [get_ports clk_b]
set_input_delay -clock clk_a 2.0 [get_ports {d_i}]
set_input_delay -clock clk_b 2.0 [get_ports {dst_rst_ni}]
set_output_delay -clock clk_b 1.0 [get_ports {q_o}]

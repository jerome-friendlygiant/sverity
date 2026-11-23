set_units -time ns
create_clock -name clk_a -period 10.0 [get_ports clk_a]
set_input_delay -clock clk_a 2.0 [get_ports {rst_ni data_i}]
set_output_delay -clock clk_a 1.0 [get_ports {data_o}]

set_units -time ns
create_clock -name clk_a -period 10.0 [get_ports clk_a]
create_clock -name clk_b -period 8.0 [get_ports clk_b]
set_clock_groups -asynchronous -group {clk_a} -group {clk_b}
set_input_delay -clock clk_a 2.0 [get_ports {vld_i pay_i}]
set_input_delay -clock clk_b 2.0 [get_ports {force_i}]
set_output_delay -clock clk_b 1.0 [get_ports {cap_o}]

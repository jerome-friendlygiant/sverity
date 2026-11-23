set_units -time ns
create_clock -name clk_io -period 10.0 [get_ports clk_io]
set_input_delay -clock clk_io 2.0 [get_ports {rst_ni cmd_i}]
set_output_delay -clock clk_io 1.0 [get_ports {resp_o}]

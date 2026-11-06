create_clock -name clk_i -period 10 [get_ports clk_i]
set_input_delay 2 -clock clk_i [get_ports payload_i]
set_output_delay 2 -clock clk_i [get_ports {payload_o active_o}]
set_false_path -from [get_ports power_on_ni]

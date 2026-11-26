create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {rst_n ev}]
set_output_delay 2 -clock clk [get_ports cnt]

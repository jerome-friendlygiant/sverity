create_clock -name clk -period 8 [get_ports clk]
set_input_delay 1 -clock clk [get_ports {din din_valid}]
set_output_delay 1 -clock clk [get_ports {dout dout_valid}]

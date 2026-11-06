create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {din din_valid}]
set_output_delay 2 -clock clk [get_ports {dout dout_valid}]

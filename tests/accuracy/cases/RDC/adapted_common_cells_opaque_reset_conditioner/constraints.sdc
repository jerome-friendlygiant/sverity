create_clock -name clk_i -period 10 [get_ports clk_i]
set_input_delay 2 -clock clk_i [get_ports din_i]
set_output_delay 2 -clock clk_i [get_ports dout_o]

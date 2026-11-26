create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {en rst_n d}]
set_output_delay 2 -clock clk [get_ports q]

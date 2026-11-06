create_clock -name clk -period 5.000 [get_ports clk]
set_input_delay 1.000 -clock clk [get_ports valid_i]

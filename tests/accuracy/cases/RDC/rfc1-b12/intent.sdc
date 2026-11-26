create_clock -name clk -period 10 [get_ports clk]
set_input_delay -clock clk -max 2 -min 0 [get_ports warm_i]

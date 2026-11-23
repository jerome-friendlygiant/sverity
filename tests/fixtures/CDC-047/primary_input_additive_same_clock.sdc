create_clock -name clk -period 10 [get_ports clk]
set_input_delay -clock clk 2 [get_ports d]
set_input_delay -clock clk -add_delay 2 [get_ports d]

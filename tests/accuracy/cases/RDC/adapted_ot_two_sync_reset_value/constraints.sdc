create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports enable_i]
set_output_delay 2 -clock clk [get_ports ready_o]

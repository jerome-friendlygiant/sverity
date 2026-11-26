create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports d]
set_output_delay 2 -clock clk [get_ports q]
set_false_path -from [get_ports ext_rst_n]

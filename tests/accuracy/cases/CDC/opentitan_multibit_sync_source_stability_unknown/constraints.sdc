create_clock -name clk_i -period 5.000 [get_ports clk_i]
set_input_delay 0.000 -clock clk_i [get_ports data_i]
set_output_delay 0.000 -clock clk_i [get_ports data_o]
set_false_path -from [get_ports rst_ni]

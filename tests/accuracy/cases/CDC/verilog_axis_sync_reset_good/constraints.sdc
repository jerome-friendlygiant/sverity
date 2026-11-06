create_clock -name clk_i -period 10.000 [get_ports clk_i]
set_output_delay 0.000 -clock clk_i [get_ports out_o]
set_false_path -from [get_ports rst_i]

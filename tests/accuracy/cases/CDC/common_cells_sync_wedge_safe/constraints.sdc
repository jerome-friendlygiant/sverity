create_clock -name clk_i -period 10.000 [get_ports clk_i]
set_input_delay 0.000 -clock clk_i [get_ports async_level_i]
set_output_delay 0.000 -clock clk_i [get_ports {level_o rising_o falling_o}]
set_false_path -from [get_ports rst_ni]

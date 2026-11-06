create_clock -name clk_i -period 10.000 [get_ports clk_i]
set_input_delay 0.000 -clock clk_i [get_ports test_mode_i]
set_output_delay 0.000 -clock clk_i [get_ports {rst_no init_no}]
set_false_path -from [get_ports async_rst_ni]

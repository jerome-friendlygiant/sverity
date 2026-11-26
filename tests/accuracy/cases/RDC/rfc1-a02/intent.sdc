create_clock -name clk -period 10 [get_ports clk]
set_input_delay 2 -clock clk [get_ports {por_n sw_rst_n we wdata}]
set_output_delay 2 -clock clk [get_ports {cfg cfg_shadow}]

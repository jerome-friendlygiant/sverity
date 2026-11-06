create_clock -name clk0_i -period 10.000 [get_ports clk0_i]
create_clock -name clk1_i -period 14.000 [get_ports clk1_i]
create_generated_clock -name mux_clk -source [get_ports clk0_i] -divide_by 1 [get_ports mux_clk]

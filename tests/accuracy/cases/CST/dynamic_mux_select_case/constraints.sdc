create_clock -name clk0_i -period 10.000 [get_ports clk0_i]
create_clock -name clk1_i -period 12.000 [get_ports clk1_i]
create_clock -name mux_clk -period 10.000 [get_ports mux_clk]
set_case_analysis 1 [get_ports sel_i]

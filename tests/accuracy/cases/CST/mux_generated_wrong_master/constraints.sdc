create_clock -name clk_a -period 10.000 [get_ports clk_a]
create_clock -name clk_b -period 12.000 [get_ports clk_b]
create_generated_clock -name mux_clk -source [get_ports clk_b] -divide_by 1 [get_ports mux_clk]

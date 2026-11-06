create_clock -name clk_i -period 10.000 [get_ports clk_i]
create_generated_clock -name slow_clk -source [get_ports clk_i] -divide_by 4 [get_ports slow_clk]

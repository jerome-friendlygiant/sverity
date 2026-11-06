create_clock -name clk_i -period 10.000 [get_ports clk_i]
create_clock -name clk_i_recovery -period 10.000 [get_ports clk_i]
create_generated_clock -name gated_clk -source [get_ports clk_i] -divide_by 1 [get_ports gated_clk]

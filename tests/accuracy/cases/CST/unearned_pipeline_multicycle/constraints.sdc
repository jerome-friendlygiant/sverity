create_clock -name clk_i -period 8.000 [get_ports clk_i]
set_multicycle_path 2 -setup -from [get_registers count_q] -to [get_registers result_o]
set_multicycle_path 1 -hold -from [get_registers count_q] -to [get_registers result_o]

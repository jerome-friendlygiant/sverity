create_clock -name clk_i -period 7.000 [get_ports clk_i]
set_multicycle_path 3 -setup -from [get_registers payload_q] -to [get_registers payload_o]
set_multicycle_path 2 -hold -from [get_registers payload_q] -to [get_registers payload_o]

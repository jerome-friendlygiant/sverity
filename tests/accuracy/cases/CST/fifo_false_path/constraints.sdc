create_clock -name clk_i -period 10.000 [get_ports clk_i]
set_false_path -from [get_ports in_rdata_i] -to [get_ports out_rdata_o]

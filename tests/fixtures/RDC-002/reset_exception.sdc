create_clock -name clka -period 10 [get_ports clka]
set_false_path -from [get_ports rst_n]

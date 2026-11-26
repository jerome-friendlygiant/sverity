create_clock -name clka -period 10 [get_ports clka]
set_false_path -to [get_ports rst_n]

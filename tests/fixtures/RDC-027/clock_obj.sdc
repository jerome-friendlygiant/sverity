create_clock -name clka -period 10 [get_ports clka]
create_clock -name rst_n -period 11 [get_ports d]
set_false_path -from [get_clocks rst_n]

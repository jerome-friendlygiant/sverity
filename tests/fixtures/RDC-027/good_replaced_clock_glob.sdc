create_clock -name old -period 10 [get_ports rst_*]
create_clock -name current -period 20 [get_ports top.rst_n]
set_false_path -from [get_clocks old]

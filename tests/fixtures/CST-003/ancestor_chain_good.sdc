create_clock -name root -period 10 [get_ports clk]
create_generated_clock -name derived -master_clock root -source [get_ports clk] -divide_by 2 [get_ports div]
create_generated_clock -name child -source [get_ports div] -divide_by 2 [get_ports leaf]
set_clock_groups -asynchronous -group {root child} -group unused

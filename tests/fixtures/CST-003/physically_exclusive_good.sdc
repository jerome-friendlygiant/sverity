create_clock -name root -period 10 [get_ports clk]
create_generated_clock -name derived -master_clock root -source [get_ports clk] -divide_by 2 [get_ports div]
set_clock_groups -physically_exclusive -group {root derived} -group unused

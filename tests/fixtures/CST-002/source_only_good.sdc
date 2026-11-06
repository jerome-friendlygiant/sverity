create_clock -name root -period 10 [get_ports clk]
create_generated_clock -name derived -source [get_ports clk] -divide_by 2 [get_ports div]

create_clock -name root -period 10 [get_ports clk]
create_generated_clock -name derived -master_clock root -source [get_ports absent] -divide_by 2 [get_ports div]

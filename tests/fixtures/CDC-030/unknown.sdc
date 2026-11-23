create_clock -name clka -period 10 [get_ports clka]
create_generated_clock -name clkdiv -source [get_ports missing_clock] -divide_by 2 [get_pins top.div]

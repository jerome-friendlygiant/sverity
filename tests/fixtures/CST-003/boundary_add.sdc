create_clock -name root -period 10 [get_ports clk]
create_clock -name root -period 12 -add [get_ports other]
create_clock -name alternate -period 8 -add [get_ports other]

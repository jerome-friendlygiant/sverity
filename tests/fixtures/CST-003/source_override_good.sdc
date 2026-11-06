create_clock -name root -period 10 [get_ports clk]
create_clock -name alternate -add -period 10 [get_ports clk]

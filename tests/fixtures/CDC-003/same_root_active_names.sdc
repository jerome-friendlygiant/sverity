create_clock -name shared -period 10 [get_ports clk]
create_clock -add -name other -period 7 [get_ports clk]
set_clock_groups -asynchronous -group {shared} -group {other}

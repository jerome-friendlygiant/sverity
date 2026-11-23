create_clock -name shared -period 10 [get_ports {clka clkb}]
create_clock -add -name other -period 1 [get_ports clka]
set_clock_groups -asynchronous -group {shared} -group {other}

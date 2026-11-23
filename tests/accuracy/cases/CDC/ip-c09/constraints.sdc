create_clock -name wr -period 8 [get_ports wr_clk]
create_clock -name rd -period 11 [get_ports rd_clk]
set_clock_groups -asynchronous -group [get_clocks wr] -group [get_clocks rd]

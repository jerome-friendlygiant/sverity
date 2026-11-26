create_clock -name clk_sys -period 10 [get_ports clk_sys]
create_clock -name clk_dbg -period 16 [get_ports clk_dbg]
set_clock_groups -asynchronous -group [get_clocks clk_sys] -group [get_clocks clk_dbg]
set_input_delay 2 -clock clk_dbg [get_ports ev]

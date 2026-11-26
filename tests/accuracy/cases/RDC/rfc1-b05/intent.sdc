create_clock -name cfg -period 10 [get_ports clk_cfg]
create_clock -name dma -period 6 [get_ports clk_dma]
set_clock_groups -asynchronous -group [get_clocks cfg] -group [get_clocks dma]

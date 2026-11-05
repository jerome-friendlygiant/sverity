create_clock -name a -period 10 [get_ports clka]
create_clock -name b -period 10 [get_ports clkb]
if {$unset} { set_clock_groups -asynchronous -group a -group b }

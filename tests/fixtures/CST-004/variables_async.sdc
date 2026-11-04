set P 10
set clocks {clka clkb}
foreach c $clocks {
  create_clock -name $c -period ${P} [get_ports $c]
}
set_clock_groups -asynchronous -group clka -group clkb

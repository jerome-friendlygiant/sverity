set P 10
create_clock -name clka -period $P [get_ports clka]
foreach c {clkb} {
  create_clock -name $c -period $undefined [get_ports $c]
}
set_clock_groups -asynchronous -group clka -group clkb

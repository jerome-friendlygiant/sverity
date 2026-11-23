// sverity-args: --top top --sdc @DIR@/comb_net_cell.sdc
// [get_cells comb_net] must NOT match a plain combinational wire; report CST-002 and mark constraints INCOMPLETE.
// expect: CST-002 WARNING
// expect-unresolved: CST-002
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  logic comb_net, a_q;
  assign comb_net = ~d;
  always_ff @(posedge clka) a_q <= comb_net;
  always_ff @(posedge clkb) q <= a_q;
endmodule

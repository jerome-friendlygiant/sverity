// sverity-args: --top top
// The only instance ties en to 1: the whole-struct write never runs, the live write drives member b only, so member a has no driver.
// expect: LNT-041 WARNING 6
module leaf(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  typedef struct packed {logic [6:0] a; logic b;} t_pair;
  t_pair r;
  always_ff @(posedge clk) if (!en) r <= d; else r.b <= d[0];
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(1'b1), .d, .q);
endmodule

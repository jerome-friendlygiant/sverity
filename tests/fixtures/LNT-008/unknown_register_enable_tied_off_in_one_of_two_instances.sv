// sverity-args: --top top
// u0 leaves the enable free and u1 ties it to 0: the shared body writes r in one instance only, and r of u1 is never driven.
// expect-unresolved: LNT-008 5
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (en) r <= d;
  assign q = r;
endmodule
module top(input logic clk, en, input logic [7:0] d, output logic [7:0] q0, q1);
  child u0(.clk, .en, .d, .q(q0));
  child u1(.clk, .en(1'b0), .d, .q(q1));
endmodule

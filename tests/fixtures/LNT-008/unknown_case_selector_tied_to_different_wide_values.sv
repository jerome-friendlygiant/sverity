// sverity-args: --top top
// u0 ties the selector to 2'd1 and u1 to 2'd2: both are nonzero but the arm 2'd1 is selected in u0 only, so u1 never drives its r.
// expect-unresolved: LNT-008 5
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) 2'd1: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q0, q1);
  child u0(.clk, .en(2'd1), .d, .q(q0));
  child u1(.clk, .en(2'd2), .d, .q(q1));
endmodule

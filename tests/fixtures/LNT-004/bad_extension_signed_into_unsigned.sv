// sverity-args: --top top
// A signed value extended into an unsigned wider target: every negative value becomes a large positive one.
// expect: LNT-004 WARNING 5
module top(input logic signed [7:0] a, output logic [15:0] y);
  assign y = a;
endmodule

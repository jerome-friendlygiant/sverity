// sverity-args: --top top
// Near-miss of bad_mixed_variable: each variable has one kind of assignment (s nonblocking, t blocking).
// expect-none: LNT-063
module top(input logic clk, a, output logic s, t);
  always @(posedge clk) s <= a;
  always_comb t = ~a;
endmodule

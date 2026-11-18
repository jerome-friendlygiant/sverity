// sverity-args: --top top
// A clock reaching an output through logic is data use, not forwarding.
// expect: LNT-027 WARNING
module top(input logic clk, en, d, output logic q, output logic gated);
  always_ff @(posedge clk) q <= d;
  assign gated = clk & en;
endmodule

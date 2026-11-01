// sverity-args: --top top
// expect-none: LNT-015
module top(input logic clk, a, output logic q, r);
  always_ff @(posedge clk) begin
    logic t;                               // block-local temporary may be blocking
    t = a;
    q <= t;
  end
  always_comb r = ~a;
endmodule

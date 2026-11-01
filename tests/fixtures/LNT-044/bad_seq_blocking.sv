// sverity-args: --top top
// r reads t before the blocking assignment to t later in the same clocked block
// expect: LNT-044 WARNING 7
module top(input logic clk, a, output logic q, r);
  logic t;
  always @(posedge clk) begin
    r = t;
    t = a;
    q <= t;
  end
endmodule

// sverity-args: --top top
// expect: LNT-080 INFO 7
// expect-none: LNT-080 WARNING
module top(input logic clk, input logic a, output logic q);
  always_ff @(posedge clk) q <= a;
  final begin
    real r;
  end
endmodule

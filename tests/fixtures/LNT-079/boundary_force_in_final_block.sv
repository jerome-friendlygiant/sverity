// sverity-args: --top top
// Inside a final block the statement is advisory: INFO, not WARNING.
// expect: LNT-079 INFO 8
// expect-none: LNT-079 WARNING
module top(input logic clk, input logic d, output logic q);
  always_ff @(posedge clk) q <= d;
  final begin
    force q = d;
  end
endmodule

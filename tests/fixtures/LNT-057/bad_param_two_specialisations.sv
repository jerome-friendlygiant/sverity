// sverity-args: --top top
// Same line, two parameter values: two findings, so two fingerprints (the runner rejects a duplicate).
// expect: LNT-057 WARNING 6
// expect-count: LNT-057 WARNING 2
module child #(parameter int N = 8) (input logic clk, input logic [15:0] a, output logic [N-1:0] q);
  always_ff @(posedge clk) q <= a;
endmodule
module top(input logic clk, input logic [15:0] a, output logic [7:0] q8, output logic [11:0] q12);
  child #(8) u8(.clk, .a, .q(q8));
  child #(12) u12(.clk, .a, .q(q12));
endmodule

// sverity-args: --top top
// expect: LNT-022 WARNING
// expect-unresolved: LNT-022
// expect-coverage: rule-execution INCOMPLETE
module top(input logic [1:0] a, input logic i, j, d, output logic q);
  wire [1:0] b = a;
  always @(posedge a[i] or negedge b[j]) q <= d;
endmodule

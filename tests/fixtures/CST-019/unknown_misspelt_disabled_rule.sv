// sverity-args: --top top --disable-rule BOGUS-999
// Unknown: misspelt disabled rule ID produces CST-019 WARNING Unresolved.
// expect: CST-019 WARNING
// expect-unresolved: CST-019
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule

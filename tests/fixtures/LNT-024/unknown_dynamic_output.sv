// sverity-args: --top top
// An output of queue type is not modelled: the result is visible as an Unresolved WARNING, never silent.
// expect: LNT-024 WARNING
module top(input logic clk, output int q[$]);
  always @(posedge clk) q.push_back(1);
endmodule

// sverity-args: --top top
// expect-unresolved: LNT-060 6
module top(input logic clk, input logic [7:0] d, output int q);
  logic [7:0] fifo [$];
  always_ff @(posedge clk) fifo.push_back(d);
  assign q = fifo.size();
endmodule

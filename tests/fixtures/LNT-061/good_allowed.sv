// sverity-args: --top top
// expect-none: LNT-061
module top #(parameter int W = 8) (input logic clk, input logic [W-1:0] a, output logic [2:0] q);
  localparam int B = $bits(a);
  localparam int L = $clog2(W);
  always_ff @(posedge clk) q <= L[2:0] + B[2:0] + $onehot(a[0] ? 2'b01 : 2'b10);
endmodule

// sverity-args: --top top
// (parameter-dependent constants are INFO under LNT-038) Body W=4 is shared by u0 and u1, body W=8 is u2; each guard uses 2**W, which a W-bit counter cannot reach. Grouped count = instances.
// expect-count: LNT-038 INFO 3
module child #(parameter W = 4) (input logic clk, output logic hit);
  logic [W-1:0] cnt;
  always_ff @(posedge clk) cnt <= cnt + 1;
  assign hit = (cnt == (1 << W));
endmodule
module top(input logic clk, output logic h1, h2, h3);
  child u0(.clk(clk), .hit(h1));
  child u1(.clk(clk), .hit(h2));
  child #(.W(8)) u2(.clk(clk), .hit(h3));
endmodule

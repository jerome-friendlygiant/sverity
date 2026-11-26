// sverity-args: --top top
// A constant-bound for loop in an async reset branch unrolls so every bit of the packed vector has a decided constant reset value and no RDC-019 loop-variable edge is emitted.
// expect-none: RDC-010
// expect-none: RDC-019
// expect-none: RDC-020
// expect-none: RDC-025
module top(input logic clk, rst_n, d, output logic q, y);
  logic [3:0] sr;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      for (int i = 0; i < 4; i++) sr[i] <= 1'b0;
    end else begin
      sr <= {sr[2:0], d};
    end
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= 1'b0;
    else q <= sr[3];
  assign y = sr[1];
endmodule

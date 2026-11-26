// sverity-args: --top top
// revert: always_ff @(posedge clk or negedge rst_n[1]) if (!rst_n[1]) r <= 1'b0; else r <= d; => always_ff @(posedge clk) r <= d;
// revert-expect-none: RDC-019
// rst_n[1] asynchronously resets r while its wire copy rst_copy[1] is captured as data by q: per-bit functional
// reuse is WARNING Heuristic (round 4 A3). When rst_n[1] is never an async reset (revert), the sibling stays quiet.
// expect: RDC-019 WARNING
module top(input logic clk, input logic [1:0] rst_n, input logic d, output logic q, r);
  wire [1:0] rst_copy = rst_n;
  always_ff @(posedge clk or negedge rst_n[0]) if (!rst_n[0]) q <= 1'b0; else q <= rst_copy[1];
  always_ff @(posedge clk or negedge rst_n[1]) if (!rst_n[1]) r <= 1'b0; else r <= d;
endmodule

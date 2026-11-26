// sverity-args: --top top
// Active-low reset tied to its active level (1'b0): register is held permanently in reset -> RDC-001 WARNING, not an RDC-011 source.
// revert: .rst_n(1'b0) => .rst_n(1'b1)
// revert-expect-none: RDC-001 WARNING
// expect: RDC-001 WARNING
// expect-none: RDC-011
module stage(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
module top(input logic clka, rst_a, d, output logic q, r);
  stage u0(.clk(clka), .rst_n(rst_a), .d(d), .q(q));
  stage u1(.clk(clka), .rst_n(1'b0),  .d(d), .q(r));
endmodule

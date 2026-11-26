// sverity-args: --top top
// One real reset source rst_a; second instance ties its active-low reset inactive (1'b1).
// Constant-tied reset is not an independent source (no RDC-011, no RDC-001 WARNING "undriven", RDC-001 INFO tied inactive).
// expect: RDC-001 INFO
// expect-none: RDC-001 WARNING
// expect-none: RDC-011
module stage(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
module top(input logic clka, clkb, rst_a, d, output logic q, r);
  stage u0(.clk(clka), .rst_n(rst_a), .d(d), .q(q));
  stage u1(.clk(clkb), .rst_n(1'b1),  .d(d), .q(r));
endmodule

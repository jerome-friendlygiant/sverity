// sverity-args: --top top
// One active-high top reset rst; assign rst_n = ~rst is an exact inverted copy -> one leaf source, so RDC-011 does not fire.
// expect-none: RDC-011
module top(input logic clka, clkb, rst, d, output logic q, r);
  logic rst_n;
  assign rst_n = ~rst;
  always_ff @(posedge clka or posedge rst)   if (rst)    q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule

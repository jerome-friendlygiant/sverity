// sverity-args: --top top
// Parent uses active-high posedge rst; child receives ~rst on active-low negedge rst_ni.
// Same leaf and same effective assertion polarity -> same reset domain: quiet on RDC-004 and RDC-011.
// expect-none: RDC-004
// expect-none: RDC-011
module child(input logic clk, rst_ni, d, output logic q);
  always_ff @(posedge clk or negedge rst_ni) if (!rst_ni) q <= 1'b0; else q <= d;
endmodule
module top(input logic clka, rst, d, output logic q);
  logic s;
  always_ff @(posedge clka or posedge rst) if (rst) s <= 1'b0; else s <= d;
  child u(.clk(clka), .rst_ni(~rst), .d(s), .q(q));
endmodule

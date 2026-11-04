// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @FIXTURES@/RDC-003/cells.json
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module RESET_FF_LOW(input CLK, D, R, output Q); endmodule
module RESET_FF_HIGH(input CLK, D, R, output Q); endmodule
module top(input logic clka, rst_n, rst, d, output logic [3:0] q);
  wire released_low, released_high;
  assign released_low = 1'b1;
  assign released_high = 1'b0;
  wire r0_0, r0_1;
  RESET_FF_LOW u0_0(.CLK(clka), .R(rst_n), .D(released_low), .Q(r0_0));
  RESET_FF_LOW u0_1(.CLK(clka), .R(rst_n), .D(r0_0), .Q(r0_1));
  always_ff @(posedge clka or negedge r0_1) if (!r0_1) q[0] <= 0; else q[0] <= d;
  wire r1_0, r1_1;
  RESET_FF_HIGH u1_0(.CLK(clka), .R(rst), .D(released_high), .Q(r1_0));
  RESET_FF_HIGH u1_1(.CLK(clka), .R(rst), .D(r1_0), .Q(r1_1));
  always_ff @(posedge clka or posedge r1_1) if (r1_1) q[1] <= 0; else q[1] <= d;
  wire r2_0, r2_1, r2_2;
  RESET_FF_LOW u2_0(.CLK(clka), .R(rst_n), .D(released_low), .Q(r2_0));
  RESET_FF_LOW u2_1(.CLK(clka), .R(rst_n), .D(r2_0), .Q(r2_1));
  RESET_FF_LOW u2_2(.CLK(clka), .R(rst_n), .D(r2_1), .Q(r2_2));
  always_ff @(posedge clka or negedge r2_2) if (!r2_2) q[2] <= 0; else q[2] <= d;
  wire r3_0, r3_1, r3_2;
  RESET_FF_HIGH u3_0(.CLK(clka), .R(rst), .D(released_high), .Q(r3_0));
  RESET_FF_HIGH u3_1(.CLK(clka), .R(rst), .D(r3_0), .Q(r3_1));
  RESET_FF_HIGH u3_2(.CLK(clka), .R(rst), .D(r3_1), .Q(r3_2));
  always_ff @(posedge clka or posedge r3_2) if (r3_2) q[3] <= 0; else q[3] <= d;
endmodule

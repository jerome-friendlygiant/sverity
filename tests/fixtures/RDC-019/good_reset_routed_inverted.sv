// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset reaches a child output and an inverter whose only uses are asynchronous control pins: reset routing, not functional use.
// expect-none: RDC-019
module route(input wire src, output wire [3:2] dst);
  assign dst = {1'b1, src};
endmodule
module reset_user(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 0; else q <= d;
endmodule
module reset_hi(input logic c, r, d, output logic q);
  always_ff @(posedge c or posedge r) if (r) q <= 0; else q <= d;
endmodule
module top(input logic clka, rst_n, d, output logic q);
  wire [3:2] bus;
  route u(rst_n, bus);
  reset_user w(clka, bus[2], d, q);
  wire inv = ~bus[2];
  wire extra;
  reset_hi v(clka, inv, d, extra);
endmodule

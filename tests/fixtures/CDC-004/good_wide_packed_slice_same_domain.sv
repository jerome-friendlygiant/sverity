// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 16512-bit packed vector (OpenTitan otp_ctrl part_buf_data) is tracked per bit: a constant slice that only clka registers
// drive is captured on clka with no crossing, although other bits of the vector come from clkb.
// expect-none: CDC-003
// expect-none: CDC-004
// expect-none: CST-004
module top(input logic clka, clkb, rst_n, input logic [7:0] da, input logic [16503:0] db, output logic [7:0] q);
  logic [7:0] a;
  logic [16503:0] b;
  logic [2063:0][7:0] wide;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a <= '0; else a <= da;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b <= '0; else b <= db;
  assign wide = {b, a};
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= '0; else q <= wide[0];
endmodule

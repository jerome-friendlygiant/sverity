// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// both RAM clocks are clka but the read data is captured by a clkb register: the crossing at the read port must not vanish
// expect: CDC-035 ERROR
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic a_q, rd;
  always_ff @(posedge clka) a_q <= d;
  DPRAM u(.WCLK(clka), .RCLK(clka), .WD(a_q), .RD(rd));
  always_ff @(posedge clkb) q <= rd;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// the write data is launched in clkb but the RAM is written on clka: crossing into the memory
// expect: CDC-035 ERROR
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic b_q, rd;
  always_ff @(posedge clkb) b_q <= d;
  DPRAM u(.WCLK(clka), .RCLK(clka), .WD(b_q), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

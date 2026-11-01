// sverity-args: --top top --library @DIR@/cells.json
// the read clock is an undriven internal net: it does not resolve to a clock root, so the crossing is unresolved (never safe)
// expect: CDC-035 WARNING
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, d, output logic rd);
  logic floating_clk;
  DPRAM u_ram(.WCLK(clka), .RCLK(floating_clk), .WD(d), .RD(rd));
endmodule

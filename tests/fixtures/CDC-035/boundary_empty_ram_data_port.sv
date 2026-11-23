// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// An unconnected nonclock port does not discard the declared RAM or its dual-clock finding.
// expect: CDC-035 WARNING
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, output logic q);
  logic rd;
  DPRAM u_ram(.WCLK(clka), .RCLK(clkb), .WD(), .RD(rd));
  always_ff @(posedge clkb) q <= rd;
endmodule

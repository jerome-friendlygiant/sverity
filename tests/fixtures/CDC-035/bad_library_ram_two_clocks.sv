// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// DPRAM is declared in cells.json as a RAM written on WCLK and read on RCLK: clka and clkb are asynchronous, so the cell is a crossing
// expect: CDC-035 WARNING
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;   // simulation stub
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic rd;
  DPRAM u_ram(.WCLK(clka), .RCLK(clkb), .WD(d), .RD(rd));
  always_ff @(posedge clkb) q <= rd;
endmodule

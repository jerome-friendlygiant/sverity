// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// both RAM clock ports, the writer of WD and the capturer of RD are in one clock domain: no crossing at the cell
// expect-none: CDC-035
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, d, output logic q);
  logic a_q, rd;
  always_ff @(posedge clka) a_q <= d;
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WD(a_q), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

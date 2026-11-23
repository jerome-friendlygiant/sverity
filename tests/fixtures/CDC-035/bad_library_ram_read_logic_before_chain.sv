// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// Logic ahead of the first capture keeps the RAM read crossing unsafe.
// expect: CDC-035 ERROR
// expect-confidence: CDC-035 established
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic a_q, rd, s1;
  always_ff @(posedge clka) a_q <= d;
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WD(a_q), .RD(rd));
  always_ff @(posedge clkb) begin s1 <= ~rd; q <= s1; end
endmodule

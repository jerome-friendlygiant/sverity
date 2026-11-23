// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// Both arms capture the RAM data, so the synchronized valid does not qualify the crossing.
// expect: CDC-035 ERROR
// expect-confidence: CDC-035 established
module DPRAM(input logic WCLK, RCLK, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, load, output logic q);
  logic a_q, rd, valid_a, v1, v2;
  always_ff @(posedge clka) begin a_q <= d; valid_a <= load; end
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WD(a_q), .RD(rd));
  always_ff @(posedge clkb) {v2, v1} <= {v1, valid_a};
  always_ff @(posedge clkb) if (v2) q <= rd; else q <= rd;
endmodule

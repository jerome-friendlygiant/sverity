// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_qualified_ram.json
// A two-stage valid synchronized into the RAM write domain qualifies the payload write.
// expect: CDC-035 WARNING
// expect-confidence: CDC-035 heuristic
module DPRAM(input logic WCLK, RCLK, WE, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, output logic q);
  logic payload_b, valid_b, v1, v2, rd;
  always_ff @(posedge clkb) begin payload_b <= d; valid_b <= valid; end
  always_ff @(posedge clka) {v2, v1} <= {v1, valid_b};
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE(v2), .WD(payload_b), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

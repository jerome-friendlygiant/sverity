// sverity-args: --top top --option CDC-005.min_stages=3 --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_qualified_ram.json
// The write enable taps stage two when stage three is required.
// expect: CDC-035 WARNING
// expect-unresolved: CDC-035
// expect-none: CDC-035 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module DPRAM(input logic WCLK, RCLK, WE, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, output logic q);
  logic payload_b, valid_b, v1, v2, v3, rd;
  always_ff @(posedge clkb) begin payload_b <= d; valid_b <= valid; end
  always_ff @(posedge clka) {v3, v2, v1} <= {v2, v1, valid_b};
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE(v2), .WD(payload_b), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3 --library @DIR@/cells_qualified_ram.json
// v2 is a two-stage tap into a three-stage synchronizer and cannot prove read-data qualification.
// expect: CDC-035 WARNING
// expect-unresolved: CDC-035
// expect-coverage: payload-qualification INCOMPLETE
module DPRAM(input logic WCLK, RCLK, WE, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, output logic q);
  logic data_a, valid_a, v1, v2, v3, rd;
  always_ff @(posedge clka) begin data_a <= d; valid_a <= valid; end
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE(1'b0), .WD(data_a), .RD(rd));
  always_ff @(posedge clkb) begin {v3, v2, v1} <= {v2, v1, valid_a}; if (v2) q <= rd; end
endmodule

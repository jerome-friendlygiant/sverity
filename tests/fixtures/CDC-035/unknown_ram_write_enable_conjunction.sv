// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_qualified_ram.json
// A conjunction contains a synchronized enable without a modeled necessity proof.
// expect: CDC-035 WARNING
// expect-unresolved: CDC-035
// expect-none: CDC-035 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module DPRAM(input logic WCLK, RCLK, WE, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, local_enable, output logic q);
  logic payload_b, valid_b, v1, v2, rd;
  always_ff @(posedge clkb) begin payload_b <= d; valid_b <= valid; end
  always_ff @(posedge clka) {v2, v1} <= {v1, valid_b};
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE(v2 & local_enable), .WD(payload_b), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

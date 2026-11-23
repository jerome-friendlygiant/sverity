// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_vector_we.json
// Both vector write-enable bits are synchronized, but the modeled write qualification is ambiguous.
// expect: CDC-035 WARNING
// expect-unresolved: CDC-035
// expect-coverage: payload-qualification INCOMPLETE
module DPRAM(input logic WCLK, RCLK, input logic [1:0] WE, input logic WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, output logic q);
  logic b_q, v_b, v1, v2, rd;
  always_ff @(posedge clkb) begin b_q <= d; v_b <= valid; end
  always_ff @(posedge clka) begin v1 <= v_b; v2 <= v1; end
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE({v2, v2}), .WD(b_q), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

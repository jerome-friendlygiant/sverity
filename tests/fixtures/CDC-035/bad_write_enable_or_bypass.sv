// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells_qualified_ram.json
// The local write-enable OR arm bypasses the synchronized qualifier.
// expect: CDC-035 ERROR
module DPRAM(input logic WCLK, RCLK, WE, WD, output logic RD);
  assign RD = WD;
endmodule
module top(input logic clka, clkb, d, valid, local_enable, output logic q);
  logic b_q, v_b, v1, v2, rd;
  always_ff @(posedge clkb) begin b_q <= d; v_b <= valid; end
  always_ff @(posedge clka) begin v1 <= v_b; v2 <= v1; end
  DPRAM u_ram(.WCLK(clka), .RCLK(clka), .WE(v2 | local_enable), .WD(b_q), .RD(rd));
  always_ff @(posedge clka) q <= rd;
endmodule

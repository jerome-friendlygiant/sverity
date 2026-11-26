// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One crossing (s2 -> clkb) reached through the scan mux r1 and again through r = r1 & ~x: reported once, at the nearest root r1.
// expect: RDC-007 ERROR
// expect-count: RDC-007 ERROR 1
module top(input logic clka, clkb, rst_n, scanmode, scan_rst_n, x, d, output logic q1, q);
  logic s1, s2, r1, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r1 = scanmode ? scan_rst_n : s2;
  always_ff @(posedge clkb or negedge r1) if (!r1) q1 <= 1'b0; else q1 <= d;
  assign r = r1 & ~x;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule

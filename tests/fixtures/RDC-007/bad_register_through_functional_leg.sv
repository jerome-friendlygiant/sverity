// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// r1 is the functional leg of r2 (scanmode=0): the clka release reaches clkb in mission mode.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, scanmode, scan_rst_n, trst_pad, d, output logic qa, q);
  logic s1, s2, r1, r2, r3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r1 = scanmode ? scan_rst_n : s2;
  always_ff @(posedge clka or negedge r1) if (!r1) qa <= 1'b0; else qa <= d;
  assign r2 = scanmode ? trst_pad : r1;
  assign r3 = scanmode ? scan_rst_n : r2;
  always_ff @(posedge clkb or negedge r3) if (!r3) q <= 1'b0; else q <= d;
endmodule

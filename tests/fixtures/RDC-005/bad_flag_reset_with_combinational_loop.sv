// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The flag reset has a combinational loop in its fan-in: what the loop carries is not known, so the source is not recognised.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, por_n, scan_rst_n, scanmode, output logic ob);
  logic l, s1, s2, r, f, b1, b2;
  always_ff @(posedge clka or negedge por_n) if (!por_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign l = r;
  assign r = scanmode ? scan_rst_n : (s2 & l);
  always_ff @(posedge clka or negedge r) if (!r) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule

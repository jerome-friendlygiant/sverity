// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The flag is reset through a scan mux whose legs are a top input and a recognised reset synchroniser: every leaf is a reset source.
// expect-none: RDC-005
module top(input logic clka, clkb, por_n, scan_rst_n, scanmode, output logic ob);
  logic s1, s2, r, f, b1, b2;
  always_ff @(posedge clka or negedge por_n) if (!por_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r = scanmode ? scan_rst_n : s2;
  always_ff @(posedge clka or negedge r) if (!r) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule

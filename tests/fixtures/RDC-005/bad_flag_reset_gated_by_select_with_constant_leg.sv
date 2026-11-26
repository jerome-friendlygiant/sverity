// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign r = x ? s2 : 1'b0; => assign r = x ? s2 : scan_rst_n;
// revert-expect-none: RDC-005
// x selects between the synchroniser and a constant: it gates the reset (x = 0 asserts the flag), so it is a data leaf, not a test-mode select.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, por_n, scan_rst_n, x, output logic ob);
  logic s1, s2, r, f, b1, b2;
  always_ff @(posedge clka or negedge por_n) if (!por_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r = x ? s2 : 1'b0;
  always_ff @(posedge clka or negedge r) if (!r) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule

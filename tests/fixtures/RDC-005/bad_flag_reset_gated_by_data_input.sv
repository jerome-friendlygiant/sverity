// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign r = s2 & x; => assign r = s2;
// revert-expect-none: RDC-005
// The flag reset is a synchroniser output gated by a data top input x: a data input is not a reset source.
// expect: RDC-005 WARNING
module top(input logic clka, clkb, por_n, x, output logic ob);
  logic s1, s2, r, f, b1, b2;
  always_ff @(posedge clka or negedge por_n) if (!por_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r = s2 & x;
  always_ff @(posedge clka or negedge r) if (!r) f <= 1'b1; else f <= 1'b0;
  always_ff @(posedge clkb) begin b1 <= f; b2 <= b1; end
  assign ob = b2;
endmodule

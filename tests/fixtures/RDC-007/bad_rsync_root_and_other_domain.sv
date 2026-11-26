// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A clka reset-synchroniser output (also a reset root of clka registers) ANDed into a clkb reset must still be reported as RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, en, d, output logic qa, q);
  logic s1, s2, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  always_ff @(posedge clka or negedge s2) if (!s2) qa <= 1'b0; else qa <= d;
  assign r = s2 & en;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule

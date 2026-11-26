// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge r) if (!r) q => always_ff @(posedge clka or negedge r) if (!r) q
// revert-expect-none: RDC-007 ERROR
// r1 (clka synchroniser output ANDed with en1) is itself a reset root; r = r1 & en2 resets clkb registers, so its release is clka-timed.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, en1, en2, d, output logic qa, q);
  logic s1, s2, r1, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= 1'b1; s2 <= s1; end
  assign r1 = s2 & en1;
  always_ff @(posedge clka or negedge r1) if (!r1) qa <= 1'b0; else qa <= d;
  assign r = r1 & en2;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule

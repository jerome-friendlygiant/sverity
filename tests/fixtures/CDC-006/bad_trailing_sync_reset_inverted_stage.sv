// sverity-args: --top top --sdc @FIXTURES@/CDC-010/good_sync_reset_sync_source_resolved.sdc
// revert: s2 <= ~s1; => s2 <= s1;
// revert-expect-none: CDC-006 ERROR
// expect: CDC-006 ERROR
// expect-confidence: CDC-006 established
// expect-none: CDC-028 ERROR
module top(input logic clka, clkb, rst_b, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin
    s1 <= a_q;
    s2 <= ~s1;
    if (rst_b) begin
      s1 <= 1'b0;
      s2 <= 1'b0;
    end
  end
  assign q = s2;
endmodule

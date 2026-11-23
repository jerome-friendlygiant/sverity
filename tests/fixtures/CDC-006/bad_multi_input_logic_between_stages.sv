// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: s2 <= s1 & en; => s2 <= s1;
// revert-expect-none: CDC-006 ERROR
// expect: CDC-006 ERROR
// expect-confidence: CDC-006 established
module top(input logic clka, clkb, rst_n, d, en, output logic q);
  logic a_q, s1, s2, out_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= a_q; s2 <= s1 & en; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) out_q <= 1'b0; else out_q <= s2;
  assign q = out_q;
endmodule

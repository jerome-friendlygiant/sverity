// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Safe lookalike: combinational gating with en occurs after two clean stages, not between stages 1 and 2.
// expect-none: CDC-006
// expect-none: CDC-007
// expect-none: CDC-003 ERROR
module top(input logic clka, clkb, rst_n, d, en, output logic q);
  logic a_q, s1, s2, out_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= a_q; s2 <= s1; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) out_q <= 1'b0; else out_q <= s2 & en;
  assign q = out_q;
endmodule

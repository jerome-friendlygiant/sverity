// sverity-args: --top top --sdc @DIR@/unknown_relation.sdc
// Reset roots differ structurally even though the clock relation is unknown; CDC-041 remains heuristic.
// expect: CDC-041 WARNING
// expect-confidence: CDC-041 heuristic
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q);
  logic src_q, s1, s2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) src_q <= 1'b0; else src_q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= src_q; s2 <= s1; end
  assign q = s2;
endmodule

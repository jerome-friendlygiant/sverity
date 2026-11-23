// sverity-args: --top top --sdc @DIR@/unknown_mixed_qualifier_sources.sdc
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 unresolved
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic src_clk, dst_clk, other_clk, en, data, foreign_in, output logic q);
  logic payload, foreign_reg, r1, r2;
  always_ff @(posedge src_clk) payload <= data;
  always_ff @(posedge other_clk) foreign_reg <= foreign_in;
  always_ff @(posedge dst_clk) begin
    r1 <= en | foreign_reg;
    r2 <= r1;
    if (r2) q <= payload;
  end
endmodule

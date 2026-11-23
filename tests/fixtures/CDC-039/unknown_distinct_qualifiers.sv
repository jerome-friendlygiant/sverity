// Separate source-data writes use different synchronized controls; no bypass is proven, but no one qualifier covers both.
// sverity-args: --top top --sdc @FIXTURES@/CDC-039/primary_input_qualification.sdc
// expect: CDC-039 WARNING
// expect-confidence: CDC-039 unresolved
// expect-none: CDC-028
module top(input logic src_clk, dst_clk, load, ld, din, output logic q);
  logic v1, v2, w1, w2;
  always_ff @(posedge dst_clk) begin v1 <= load; v2 <= v1; w1 <= ld; w2 <= w1; end
  always_ff @(posedge dst_clk) if (v2) q <= din; else if (w2) q <= din;
endmodule

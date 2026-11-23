// Local-only else data is not a write of the crossing payload.
// sverity-args: --top top --sdc @FIXTURES@/CDC-039/primary_input_qualification.sdc
// expect: CDC-039 WARNING
// expect-none: CDC-028
module top(input logic src_clk, dst_clk, load, din, sel, output logic q);
  logic valid, v1, v2, data, loc;
  always_ff @(posedge src_clk) begin valid <= load; data <= din; end
  always_ff @(posedge dst_clk) begin v1 <= valid; v2 <= v1; loc <= sel; end
  always_ff @(posedge dst_clk) if (v2) q <= data; else q <= loc;
endmodule

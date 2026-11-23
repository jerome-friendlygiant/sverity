// A direct self assignment in the else arm adds no source data write.
// sverity-args: --top top --sdc @FIXTURES@/CDC-039/primary_input_qualification.sdc
// expect: CDC-039 WARNING
// expect-none: CDC-028
module top(input logic src_clk, dst_clk, load, din, output logic q);
  logic v1, v2;
  always_ff @(posedge dst_clk) begin v1 <= load; v2 <= v1; end
  always_ff @(posedge dst_clk) if (v2) q <= din; else q <= q;
endmodule
